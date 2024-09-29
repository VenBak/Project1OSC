/**
	* Shell framework
	* course Operating Systems
	* Radboud University
	* v22.09.05

	Student names:
	- ...
	- ...
*/

/**
 * Hint: in most IDEs (Visual Studio Code, Qt Creator, neovim) you can:
 * - Control-click on a function name to go to the definition
 * - Ctrl-space to auto complete functions and variables
 */

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <list>
#include <optional>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
  vector<string> parts = {};
};

struct Expression {
  vector<Command> commands;
  string inputFromFile;
  string outputToFile;
  bool background = false;
};

// Parses a string to form a vector of arguments. The separator is a space char (' ').
vector<string> split_string(const string& str, char delimiter = ' ') {
  vector<string> retval;
  for (size_t pos = 0; pos < str.length(); ) {
    // look for the next space
    size_t found = str.find(delimiter, pos);
    // if no space was found, this is the last word
    if (found == string::npos) {
      retval.push_back(str.substr(pos));
      break;
    }
    // filter out consequetive spaces
    if (found != pos)
      retval.push_back(str.substr(pos, found-pos));
    pos = found+1;
  }
  return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
  // build argument list
  const char** c_args = new const char*[args.size()+1];
  for (size_t i = 0; i < args.size(); ++i) {
    c_args[i] = args[i].c_str();
  }
  c_args[args.size()] = nullptr;
  // replace current process with new process as specified
  int rc = ::execvp(c_args[0], const_cast<char**>(c_args));
  // if we got this far, there must be an error
  int error = errno;
  // in case of failure, clean up memory (this won't overwrite errno normally, but let's be sure)
  delete[] c_args;
  errno = error;
  return rc;
}

// Executes a command with arguments. In case of failure, returns error code.
int execute_command(const Command& cmd) {
  auto& parts = cmd.parts;
  if (parts.size() == 0)
    return EINVAL;

  // execute external commands
  int retval = execvp(parts);
  return retval ? errno : 0;
}

void display_prompt() {
  char buffer[512];
  char* dir = getcwd(buffer, sizeof(buffer));
  if (dir) {
    cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
  }
  cout << "$ ";
  flush(cout);
}

string request_command_line(bool showPrompt) {
  if (showPrompt) {
    display_prompt();
  }
  string retval;
  getline(cin, retval);
  return retval;
}

// note: For such a simple shell, there is little need for a full-blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parse_command_line(string commandLine) {
  Expression expression;
  vector<string> commands = split_string(commandLine, '|');
  for (size_t i = 0; i < commands.size(); ++i) {
    string& line = commands[i];
    vector<string> args = split_string(line, ' ');
    if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
      expression.background = true;
      args.resize(args.size()-1);
    }
    if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
      expression.outputToFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
      expression.inputFromFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    expression.commands.push_back({args});
  }
  return expression;
}

int external_command(Expression& expression) {
  // Initial pipe for first parent and its child
  int fd[2];
  pipe(fd);

  // Variable 'child_id[SIZE]' is an array of process ids of the child processes
  int const SIZE = (int)expression.commands.size();
  pid_t child_id[SIZE];

  bool is_background = expression.background;

  if (!expression.inputFromFile.empty()) {
    int input_fd = open(expression.inputFromFile.c_str(), O_RDONLY); // Open for reading only
    if (input_fd == -1) {
      perror("Failed to open input file");
      return -1;
    }
    dup2(input_fd, STDIN_FILENO);
    close(input_fd);
  }
  
  // Handle output redirection to file
  bool output_redirect = !expression.outputToFile.empty();
  int output_fd = -1;
  if (output_redirect) {
    output_fd = open(expression.outputToFile.c_str(), O_WRONLY); // Open for writing only
    if (output_fd == -1) {
      perror("Failed to open output file");
      return -1;
    }
  }

  // Two possibilities exist: SIZE=1 or SIZE>1. Let's consider what if SIZE=1
  if(SIZE == 1){
    child_id[0] = fork();
    if(child_id[0] == 0){
      Command cmd = expression.commands[0];
      dup2(fd[0], STDIN_FILENO);
      execute_command(cmd);
      abort();
    }
    // Close the parent pipe
    close(fd[0]);
    close(fd[1]);
  }
  // Now consider the possibility that SIZE>1
  else {
    // Variable 'prev_fd' saves the read end of the previous pipe
    int prev_fd = fd[0];  

    /* A loop goes over all commands. Each iteration creates a new process. 
    Each iteration is responsible for the execution of one command*/
    for (int j = 0; j < SIZE; j++) {
      // Create a pipe for the current command - exc. the last one
      int cfd[2];
      if (j < SIZE - 1) {
        pipe(cfd);
      }

      child_id[j] = fork();
      if (child_id[j] == 0) {
        Command cmd = expression.commands[j]; 

        if (j > 0) {
          // Redirect input
          dup2(prev_fd, STDIN_FILENO);
          close(prev_fd);
        }

        if (j < SIZE - 1) {
          // Redirect output
          close(cfd[0]);
          dup2(cfd[1], STDOUT_FILENO);
          close(cfd[1]);
        }

        else if (output_redirect) {
          dup2(output_fd, STDOUT_FILENO);
          close(output_fd);
        }

        execute_command(cmd);
        exit(0);
      }

      // Close the parent pipe
      // First the read end of the previous pipe
      if (j > 0) 
        close(prev_fd);
      // Second the write end of the next pipe
      // ALSO: update 'prev_fd' to the current pipe's read end
      if (j < SIZE - 1) {
        close(cfd[1]); 
        prev_fd = cfd[0];
      }
    }
  }

  // Parent process waits for all child processes to finish
  for (int j = 0; j < SIZE; j++) {
    waitpid(child_id[j], nullptr, 0);
  }
  
  if (output_redirect) {
    close(output_fd);
  }

  return 0;
}

int execute_expression(Expression& expression) {
  // Check for empty expression
  if (expression.commands.size() == 0)
    return EINVAL;

  // Handle intern commands (like 'cd' and 'exit')
  if (expression.commands[0].parts[0] == "cd")
    chdir(expression.commands[0].parts[1].c_str());

  if (expression.commands[0].parts[0] == "exit") 
    exit(0);
  
  // External commands, executed with fork():
  // Loop over all commandos, and connect the output and input of the forked processes

  // For now, we just execute the first command in the expression. Disable.
  execute_command(expression.commands[0]);

  return 0;
}

// framework for executing "date | tail -c 5" using raw commands
// two processes are created, and connected to each other
int step1(bool showPrompt) {
  // create communication channel shared between the two processes
  // ...
  int fd[2];
  pipe(fd);

  pid_t child1 = fork();
  if (child1 == 0) {
    // redirect standard output (STDOUT_FILENO) to the input of the shared communication channel
    // free non used resources (why?)
    close(fd[0]);
    dup2(fd[1], STDOUT_FILENO);
    Command cmd = {{string("date")}};
    execute_command(cmd);
    close(fd[1]);
    // display nice warning that the executable could not be found
    abort(); // if the executable is not found, we should abort. (why?)
  }

  pid_t child2 = fork();
  if (child2 == 0) {
    // redirect the output of the shared communication channel to the standard input (STDIN_FILENO).
    // free non used resources (why?)
    close(fd[1]);
    dup2(fd[0], STDIN_FILENO);
    Command cmd = {{string("tail"), string("-c"), string("5")}};
    execute_command(cmd);
    close(fd[0]);
    abort(); // if the executable is not found, we should abort. (why?)
  }

  // free non used resources (why?)
  close(fd[0]);
  close(fd[1]);
  // wait on child processes to finish (why both?)
  waitpid(child1, nullptr, 0);
  waitpid(child2, nullptr, 0);
  return 0;
}

int shell(bool showPrompt) {
  //* <- remove one '/' in front of the other '/' to switch from the normal code to step1 code
  while (cin.good()) {
    string commandLine = request_command_line(showPrompt);
    Expression expression = parse_command_line(commandLine);
    int rc = execute_expression(expression);
    if (rc != 0)
      cerr << strerror(rc) << endl;
  }
  return 0;
  /*/
  return step1(showPrompt);
  //*/
}
