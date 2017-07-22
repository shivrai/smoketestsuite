//
// Copyright 2017 Shivansh Rai
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
// $FreeBSD$

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unordered_set>
#include "generate_test.h"
#include "add_testcase.h"
#include "read_annotations.h"

pair<string, int>
exec(const char* cmd)
{
  array<char, 128> buffer;
  string usage_output;
  FILE* pipe = popen(cmd, "r");

  if (!pipe) throw runtime_error("popen() failed!");
  try {
    while (!feof(pipe)) {
        if (fgets(buffer.data(), 128, pipe) != NULL)
            usage_output += buffer.data();
    }
  }
  catch(...) {
    pclose(pipe);
    throw "Unable to execute the command: " + string(cmd);
  }

  return make_pair<string, int>
         ((string)usage_output, WEXITSTATUS(pclose(pipe)));
}

void
generate_test(string utility)
{
  list<utils::opt_rel*> ident_opt_list;  // List of identified option relations.
  string test_file;             // atf-sh test name.
  string testcase_list;         // List of testcases.
  string command;               // Command to be executed in shell.
  string descr;                 // Testcase description.
  string testcase_buffer;       // Buffer for (temporarily) holding testcase data.
  ofstream test_fstream;        // Output stream for the atf-sh test.
  ifstream license_fstream;     // Input stream for license.
  pair<string, int> output;     // Return value type for `exec()`.
  unordered_set<char> annot;

  // Read annotations and populate hash set "annot".
  read_annotations(utility, annot);

  utils::opt_def f_opts;
  ident_opt_list = f_opts.check_opts(utility);
  test_file = "generated_tests/" + utility + "_test.sh";

  // Add license in the generated test scripts.
  test_fstream.open(test_file, ios::out);
  license_fstream.open("license", ios::in);

  test_fstream << license_fstream.rdbuf();
  license_fstream.close();

  // If a known option was encountered (i.e. `ident_opt_list` is
  // populated), produce a testcase to check the validity of the
  // result of that option. If no known option was encountered,
  // produce testcases to verify the correct (generated) usage
  // message when using the supported options incorrectly.

  // Add testcases for known options.
  if (!ident_opt_list.empty()) {
    for (const auto &i : ident_opt_list) {
      command = utility + " -" + i->value + " 2>&1";
      output = exec(command.c_str());
      if (!output.first.compare(0, 6, "usage:")) {
        add_known_testcase(i->value, utility, descr,
                           output.first, test_fstream);
      }
      else {
        // A usage message was produced, i.e. we
        // failed to guess the correct usage.
        add_unknown_testcase(i->value, utility, output.first, testcase_buffer);
      }
      testcase_list.append("\tatf_add_test_case " + i->value + "_flag\n");
    }
  }

  // Add testcases for the options whose usage is not known (yet).
  if (!f_opts.opt_list.empty()) {

    // For the purpose of adding a "$usage_output" variable,
    // we choose the option which produces one.
    // TODO Avoid multiple executions of an option
    for (const auto &i : f_opts.opt_list) {
      command = utility + " -" + i + " 2>&1";
      output = exec(command.c_str());
      if (!output.first.compare(0, 5, "usage") ||
          !output.first.compare(0, 5, "Usage") &&
          output.second) {
        test_fstream << "usage_output=\'" + output.first + "\'\n\n";
        break;
      }
    }

    // Execute the utility with supported options
    // and add (+ve)/(-ve) tests accordingly.
    for (const auto &i : f_opts.opt_list) {
      // If the option is annotated, skip it.
      if (annot.find(i) != annot.end())
        continue;

      command = utility + " -" + i + " 2>&1";
      output = exec(command.c_str());

      if (output.second) {
        // Non-zero exit status was encountered.
        add_unknown_testcase(string(1, i),
                             utility, output.first, testcase_buffer);
      }
      else {
        // EXIT_SUCCESS was encountered. Hence,
        // the guessed usage was correct.
        add_known_testcase(string(1, i), utility,
                           "", output.first, test_fstream);
        testcase_list.append(string("\tatf_add_test_case ")
                            + i + "_flag\n");
      }
    }

    testcase_list.append("\tatf_add_test_case invalid_usage\n");
    test_fstream << string("atf_test_case invalid_usage\ninvalid_usage_head()\n")
                  + "{\n\tatf_set \"descr\" \"Verify that an invalid usage "
                  + "with a supported option produces a valid error message"
                  + "\"\n}\n\ninvalid_usage_body()\n{";

    test_fstream << testcase_buffer + "\n}\n\n";
  }

  // Add a testcase under "no_arguments" for
  // running the utility without any arguments.
  if (annot.find('*') == annot.end()) {
    command = utility + " 2>&1";
    output = exec(command.c_str());
    add_noargs_testcase(utility, output, test_fstream);

    testcase_list.append("\tatf_add_test_case no_arguments\n");
  }

  test_fstream << "atf_init_test_cases()\n{\n" + testcase_list + "}\n";
  test_fstream.close();
}

int
main()
{
  // TODO: Walk the src tree.
  list<string> utility_list = { "date", "ln", "stdbuf" };
  string test_file;     // atf-sh test name.
  struct stat buffer;
  char answer;          // User input to determine overwriting of test files.
  int flag = 0;

  for (const auto &util : utility_list) {
    test_file = "generated_tests/" + util + "_test.sh";

    // Check if the test file exists.
    // In case the test file exists, confirm before proceeding.
    if (stat (test_file.c_str(), &buffer) == 0 && !flag) {
      cout << "Test file(s) already exists. Overwrite? [Y/n] ";
      cin >> answer;
      switch (answer) {
        case 'n':
        case 'N':
          cout << "Stopping execution!" << endl;
          flag = 1;
          break;
        default:
          // TODO capture newline character
          flag = 1;
      }
    }

    generate_test(util);
  }

  return 0;
}
