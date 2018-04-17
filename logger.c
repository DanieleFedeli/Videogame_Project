#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"

int logger_shouldStop;

void signalHandlerForLoggerProcess(int sig_no){
		logger_shouldStop = 1;
}

void startLogger(int logfile_desc, int logging_pipe[2]){
	int ret;

  ret = close(logging_pipe[1]);
  ERROR_HELPER(ret, "Cannot close pipe's write descriptor in Logger");

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = &signalHandlerForLoggerProcess;
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);

  char toWrite[BUFFERSIZE];
  logger_shouldStop = 0;
  while(1) {
    ret = read(logging_pipe[0], toWrite, BUFFERSIZE);
    if (ret == -1 && errno != EINTR) break;
    if (ret == 0) break;
 
    ret = write(logfile_desc, toWrite, ret);

    if (ret == -1 && errno != EINTR) break;
		if (logger_shouldStop) break;
   }

  ret = close(logfile_desc);
  ERROR_HELPER(ret, "Cannot close log file from Logger");

  close(logging_pipe[0]); // what happens when you write on a closed pipe?
  ERROR_HELPER(ret, "Cannot close pipe's read descriptor in Logger");

  exit(EXIT_SUCCESS);
}
