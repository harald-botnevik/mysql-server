/*
   Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.

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

/*
 * This assortment of classes is a mock http://en.wikipedia.org/wiki/Mock_object
 * implementation of junit http://en.wikipedia.org/wiki/Junit. It contains annotations,
 * classes, and interfaces that mock junit for use with test classes 
 * that use a subset of junit functionality. 
 * <p>
 * In clusterj, test classes can use either the real junit or this mock junit.
 * The mock can be used stand-alone or invoked by the maven surefire junit plugin.
 * Other test runners and harnesses might not have been tested and might not work.
 * <p>
 * There is no code copied from Junit itself. Only concepts and names of
 * annotations, interfaces, classes, and methods are copied, which must exactly match
 * the corresponding items from junit in order to be mocked.
 */

package junit.textui;

import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;

import junit.framework.AssertionFailedError;
import junit.framework.Test;
import junit.framework.TestListener;

/** This class implements TestListener which monitors the execution of tests and tracks errors and failures.
 * It is implemented as part of the test runner framework. For each error and failure, collect the relevant
 * information in a message buffer.
 * When the test is over, the error and failure information is printed in reportErrors();
 */
public class ResultPrinter implements TestListener {

    /** the test number */
    int testNumber = 0;

    /** the printer */
    PrintStream printer = System.out;

    /** the message buffer */
    StringBuilder messages = new StringBuilder();

    /** An error (exception) occurred during the execution of the test.
     */
    public void addError(Test test, Throwable t) {
        printer.print("ERROR...");
        messages.append(testNumber);
        messages.append(": ");
        messages.append(test.toString());
        messages.append(" FAILED:\n");
        StringWriter stringWriter = new StringWriter();
        PrintWriter stackPrinter = new PrintWriter(stringWriter);
        t.printStackTrace(stackPrinter);
        messages.append(stringWriter.toString());
        messages.append("\n");
    }

    /** A failure (junit assertion) occurred during the execution of the test.
     */
    public void addFailure(Test test, AssertionFailedError t) {
        printer.print("FAILURE...");
        messages.append(testNumber);
        messages.append(": ");
        messages.append(test.toString());
        messages.append(" FAILED:\n");
        messages.append(t.getMessage());
        messages.append("\n");
    }

    /** A test ended.
     */
    public void endTest(Test test) {
        printer.println();
    }

    /** A test started.
     */
    public void startTest(Test test) {
        testNumber++;
        printer.print(testNumber + ": " + test.toString() + " running...");
    }

    /** Print the results of the test to the report printer.
     */
    public void reportErrors() {
        if (messages.length() > 0) {
            printer.println("There were test failures:\n");
            printer.println(messages.toString());
        }
    }

}