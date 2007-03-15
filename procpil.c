#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <process.h>
#include "httppil.h"
#include "procpil.h"

#define SHELL_BUFFER_SIZE 256
#ifdef WIN32
#define read _read
#define open _open
#define close _close
#endif

int ShellRead(SHELL_PARAM* param)
{
	int offset=0;
	int ret;

	if (!param->buffer || !param->iBufferSize) {
		param->flags|=SF_ALLOC;
		param->buffer=malloc(SHELL_BUFFER_SIZE);
		param->iBufferSize=SHELL_BUFFER_SIZE;
	}

	for(;;) {
		ret=read(param->fdStdoutRead, param->buffer + offset, param->iBufferSize - offset - 1);
		if (ret <= 0) break;
		offset += ret;
		if (!(param->flags & SF_LOOP_READ)) break;
		if (offset >= param->iBufferSize-1) {
			if  (!(param->flags & SF_ALLOC)) break;
			param->iBufferSize+=SHELL_BUFFER_SIZE;
			param->buffer=realloc(param->buffer,param->iBufferSize);
		}
	}
	param->buffer[offset]=0;
	return (ret>0)?offset:-1;
}

int ShellTerminate(SHELL_PARAM* param)
{
	return -1;
}

void ShellClean(SHELL_PARAM* param)
{
	if (param->fdStdoutRead) close(param->fdStdoutRead);
	if (param->fdStdinWrite) close(param->fdStdinWrite);
	if (param->fdStderrRead) close(param->fdStderrRead);
	param->fdStdoutRead=0;
	param->fdStdinWrite=0;
	param->fdStderrRead=0;
	if (param->flags & SF_ALLOC) {
		free(param->buffer);
		param->buffer=NULL;
		param->iBufferSize=0;
	}
}

int ShellWait(SHELL_PARAM* param, int iTimeout)
{
	return -1;
}

int ShellSetPriority(SHELL_PARAM* param, int iPriority)
{
	return -1;
}

int MakePipe(int* readpipe, int* writepipe, int bufsize, BOOL inheritRead, BOOL inheritWrite)
{
	int fdpipe[2];
	if (_pipe(fdpipe, bufsize, O_NOINHERIT|O_BINARY)) return -1;
	if (inheritRead) {
		*readpipe = _dup(fdpipe[READ_FD]);
		_close(fdpipe[READ_FD]);
	} else {
		*readpipe = fdpipe[READ_FD];
	}
	if (inheritWrite) {
		*writepipe = _dup(fdpipe[WRITE_FD]);
		_close(fdpipe[WRITE_FD]);
	} else {
		*writepipe = fdpipe[WRITE_FD];
	}
	return 0;
}

char** Tokenize(char* str, char delimiter)
{
	char** tokens;
	int n = 1;
	int i;
	char *p;
	
	// find out number of tokens
	p = str;
	for (;;) {
		while (*p && *p != delimiter) p++;
		if (!*p) break;
		n++;
		while (*(++p) == delimiter);
	}
	// allocate buffer for array
	tokens = (char**)malloc((n + 1) * sizeof(char*));
	// store pointers to tokens
	p = str;
	for (i = 0; i < n; i++) {
		tokens[i] = p;
		while (*p && *p != delimiter) p++;
		if (!*p) break;
		*p = 0;
		while (*(++p) == delimiter);
	}
	tokens[n] = 0;
	return tokens;
}

int ShellExec(SHELL_PARAM* param, char* commandLine)
{
	char *oldCurDir = 0;
	char **tokens;
	char *exe;
	int fdStdinRead = 0, fdStdinOld = 0;
	int fdStdinWrite = 0, fdStdoutOld = 0;
	int fdStderrWrite = 0, fdStderrOld = 0;
	tokens = Tokenize(commandLine, ' ');
	if (tokens[0][0] == '\"') {
		char *p;
		exe = _strdup(tokens[0] + 1);
		p = strrchr(exe, '\"');
		if (p) *p = 0;
	} else {
		exe = _strdup(tokens[0]);
	}
	if (param->flags & SF_REDIRECT_STDIN) {
		MakePipe(&fdStdinRead, &param->fdStdinWrite, 16384, FALSE, FALSE);
		fdStdinOld = _dup(0);
		_dup2(fdStdinRead, 0);
	}
	if (param->flags & SF_REDIRECT_STDOUT) {
		MakePipe(&param->fdStdoutRead, &fdStdinWrite, 16384, FALSE, FALSE);
		fdStdoutOld = _dup(1);
		_dup2(fdStdinWrite, 1);
		if ((param->flags & SF_REDIRECT_OUTPUT) == SF_REDIRECT_OUTPUT) {
			fdStderrOld = _dup(2);
			_dup2(fdStdinWrite, 2);
		}
	}
	if (param->flags & SF_REDIRECT_STDERR) {
		MakePipe(&param->fdStdoutRead, &fdStderrWrite, 16384, FALSE, FALSE);
		fdStderrOld = _dup(2);
		_dup2(fdStderrWrite, 2);
	}
	if (param->env) {
		param->hproc = _spawnvpe( _P_NOWAIT, exe, tokens, param->env);
	} else {
		param->hproc = _spawnvp( _P_NOWAIT, exe, tokens);
	}
	free(exe);
	free(tokens);
	if (fdStdinRead) _close(fdStdinRead);
	if (fdStdinOld) _dup2( fdStdinOld, 0);
	if (fdStdinWrite) _close(fdStdinWrite);
	if (fdStdoutOld) _dup2( fdStdoutOld, 1 );
	if (fdStderrWrite) _close(fdStderrWrite);
	if (fdStderrOld) _dup2( fdStderrOld, 2 );
	if (param->hproc < 0) {
		param->hproc = 0;
		ShellClean(param);
		return -1;
	}

	return 0;
}
