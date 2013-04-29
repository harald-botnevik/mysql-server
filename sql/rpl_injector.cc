/*
   Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "mysql_priv.h" 
#include "rpl_injector.h"

/*
  injector::transaction - member definitions
*/

/* inline since it's called below */
inline
injector::transaction::transaction(MYSQL_BIN_LOG *log, THD *thd)
  : m_state(START_STATE), m_thd(thd)
{
  /* 
     Default initialization of m_start_pos (which initializes it to garbage).
     We need to fill it in using the code below.
  */
  LOG_INFO log_info;
  log->get_current_log(&log_info);
  /* !!! binlog_pos does not follow RAII !!! */
  m_start_pos.m_file_name= my_strdup(log_info.log_file_name, MYF(0));
  m_start_pos.m_file_pos= log_info.pos;

#ifndef MCP_BUG54854
  if (unlikely(m_start_pos.m_file_name == NULL))
  {
    m_thd= NULL;
    return;
  }

  /*
     Next pos is unknown until after commit of the Binlog transaction
  */
  m_next_pos.m_file_name= 0;
  m_next_pos.m_file_pos= 0;

  /*
    Ensure we don't pickup this thd's last written Binlog pos in
    empty-transaction-commit cases
    This is not ideal, as it zaps this information for any other
    usage (e.g. WL4047)
    Potential improvement : save the 'old' next pos prior to
    commit, and restore on error.
  */
  m_thd->clear_next_event_pos();
#endif

  begin_trans(m_thd);

  thd->set_current_stmt_binlog_row_based();
}

injector::transaction::~transaction()
{
  if (!good())
    return;

#ifndef MCP_BUG54854
  /* Needed since my_free expects a 'char*' (instead of 'void*'). */
  char* const start_pos_memory= const_cast<char*>(m_start_pos.m_file_name);

  if (start_pos_memory)
  {
    my_free(start_pos_memory, MYF(0));
  }

  char* const next_pos_memory= const_cast<char*>(m_next_pos.m_file_name);
  if (next_pos_memory)
  {
    my_free(next_pos_memory, MYF(0));
  }
#endif
}

/**
   @retval 0 transaction committed
   @retval 1 transaction rolled back
 */
int injector::transaction::commit()
{
   DBUG_ENTER("injector::transaction::commit()");
   int error= m_thd->binlog_flush_pending_rows_event(true);
   /*
     Cluster replication does not preserve statement or
     transaction boundaries of the master.  Instead, a new
     transaction on replication slave is started when a new GCI
     (global checkpoint identifier) is issued, and is committed
     when the last event of the check point has been received and
     processed. This ensures consistency of each cluster in
     cluster replication, and there is no requirement for stronger
     consistency: MySQL replication is asynchronous with other
     engines as well.

     A practical consequence of that is that row level replication
     stream passed through the injector thread never contains
     COMMIT events.
     Here we should preserve the server invariant that there is no
     outstanding statement transaction when the normal transaction
     is committed by committing the statement transaction
     explicitly.
   */
   error |= ha_autocommit_or_rollback(m_thd, error);
   end_trans(m_thd, error ? ROLLBACK : COMMIT);

#ifndef MCP_BUG54854
   /* Copy next position out into our next pos member */
   if ((error == 0) &&
       (m_thd->binlog_next_event_pos.file_name != NULL) &&
       ((m_next_pos.m_file_name=
         my_strdup(m_thd->binlog_next_event_pos.file_name, MYF(0))) != NULL))
   {
     m_next_pos.m_file_pos= m_thd->binlog_next_event_pos.pos;
   }
   else
   {
     /* Error, problem copying etc. */
     m_next_pos.m_file_name= NULL;
     m_next_pos.m_file_pos= 0;
   }
#endif

   DBUG_RETURN(error);
}

#ifndef MCP_BUG58082
int injector::transaction::rollback()
{
   DBUG_ENTER("injector::transaction::rollback()");
   ha_autocommit_or_rollback(m_thd, 1 /* error to get rollback */);
   end_trans(m_thd, ROLLBACK);
   DBUG_RETURN(0);
}
#endif

int injector::transaction::use_table(server_id_type sid, table tbl)
{
  DBUG_ENTER("injector::transaction::use_table");

  int error;

  if ((error= check_state(TABLE_STATE)))
    DBUG_RETURN(error);

  server_id_type save_id= m_thd->server_id;
  m_thd->set_server_id(sid);
  error= m_thd->binlog_write_table_map(tbl.get_table(),
                                       tbl.is_transactional());
  m_thd->set_server_id(save_id);
  DBUG_RETURN(error);
}


int injector::transaction::write_row (server_id_type sid, table tbl, 
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type record
#ifndef MCP_WL5353
                                      ,const uchar* extra_row_info
#endif
                                      )
{
   DBUG_ENTER("injector::transaction::write_row(...)");
 
   int error= check_state(ROW_STATE);
   if (error)
     DBUG_RETURN(error);

   server_id_type save_id= m_thd->server_id;
   m_thd->set_server_id(sid);
   error= m_thd->binlog_write_row(tbl.get_table(), tbl.is_transactional(), 
                                  cols, colcnt, record
#ifndef MCP_WL5353
                                  , extra_row_info
#endif
                                  );
   m_thd->set_server_id(save_id);
   DBUG_RETURN(error);
}

#ifndef MCP_WL5353
int injector::transaction::write_row (server_id_type sid, table tbl,
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type record)
{
  return write_row(sid, tbl, cols, colcnt, record, NULL);
}
#endif

int injector::transaction::delete_row(server_id_type sid, table tbl,
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type record
#ifndef MCP_WL5353
                                      ,const uchar* extra_row_info
#endif
                                      )
{
   DBUG_ENTER("injector::transaction::delete_row(...)");

   int error= check_state(ROW_STATE);
   if (error)
     DBUG_RETURN(error);

   server_id_type save_id= m_thd->server_id;
   m_thd->set_server_id(sid);
   error= m_thd->binlog_delete_row(tbl.get_table(), tbl.is_transactional(), 
                                   cols, colcnt, record
#ifndef MCP_WL5353
                                   , extra_row_info
#endif
                                   );
   m_thd->set_server_id(save_id);
   DBUG_RETURN(error);
}

#ifndef MCP_WL5353
int injector::transaction::delete_row(server_id_type sid, table tbl,
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type record)
{
  return delete_row(sid, tbl, cols, colcnt, record, NULL);
}
#endif

int injector::transaction::update_row(server_id_type sid, table tbl, 
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type before, record_type after
#ifndef MCP_WL5353
                                      ,const uchar* extra_row_info
#endif
                                      )
{
   DBUG_ENTER("injector::transaction::update_row(...)");

   int error= check_state(ROW_STATE);
   if (error)
     DBUG_RETURN(error);

   server_id_type save_id= m_thd->server_id;
   m_thd->set_server_id(sid);
   error= m_thd->binlog_update_row(tbl.get_table(), tbl.is_transactional(),
                                   cols, colcnt, before, after
#ifndef MCP_WL5353
                                   , extra_row_info
#endif
                                   );
   m_thd->set_server_id(save_id);
   DBUG_RETURN(error);
}

#ifndef MCP_WL5353
int injector::transaction::update_row(server_id_type sid, table tbl,
				      MY_BITMAP const* cols, size_t colcnt,
				      record_type before, record_type after)
{
  return update_row(sid, tbl, cols, colcnt, before, after, NULL);
}
#endif

injector::transaction::binlog_pos injector::transaction::start_pos() const
{
   return m_start_pos;			
}

#ifndef MCP_BUG54854
injector::transaction::binlog_pos injector::transaction::next_pos() const
{
   return m_next_pos;
}
#endif


/*
  injector - member definitions
*/

/* This constructor is called below */
inline injector::injector()
{
}

static injector *s_injector= 0;
injector *injector::instance()
{
  if (s_injector == 0)
    s_injector= new injector;
  /* "There can be only one [instance]" */
  return s_injector;
}

void injector::free_instance()
{
  injector *inj = s_injector;

  if (inj != 0)
  {
    s_injector= 0;
    delete inj;
  }
}


injector::transaction injector::new_trans(THD *thd)
{
   DBUG_ENTER("injector::new_trans(THD*)");
   /*
     Currently, there is no alternative to using 'mysql_bin_log' since that
     is hardcoded into the way the handler is using the binary log.
   */
   DBUG_RETURN(transaction(&mysql_bin_log, thd));
}

void injector::new_trans(THD *thd, injector::transaction *ptr)
{
   DBUG_ENTER("injector::new_trans(THD *, transaction *)");
   /*
     Currently, there is no alternative to using 'mysql_bin_log' since that
     is hardcoded into the way the handler is using the binary log. 
   */
   transaction trans(&mysql_bin_log, thd);
   ptr->swap(trans);

   DBUG_VOID_RETURN;
}

int injector::record_incident(THD *thd, Incident incident)
{
  Incident_log_event ev(thd, incident);
  if (int error= mysql_bin_log.write(&ev))
    return error;
  return mysql_bin_log.rotate_and_purge(RP_FORCE_ROTATE);
}

int injector::record_incident(THD *thd, Incident incident, LEX_STRING const message)
{
  Incident_log_event ev(thd, incident, message);
  if (int error= mysql_bin_log.write(&ev))
    return error;
  return mysql_bin_log.rotate_and_purge(RP_FORCE_ROTATE);
}