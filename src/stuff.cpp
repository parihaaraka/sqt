#include "stuff.h"
#include <stdlib.h>
#include <string.h>

//extern ConfReader *conf;

unsigned short win_cp1251[128]=
{ 0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
  0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
  0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
  0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
  0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
  0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
  0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
  0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
  0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
  0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
  0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
  0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
  0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
  0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
  0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};


int utf16le_to_utf8(unsigned int c, char *outbuf)  // unicode char -> utf8 char
{
	unsigned int len ;
	int first;
	if (c < 0x80)	 {first = 0; len = 1;}
	else if (c < 0x800)	 {first = 0xc0;len = 2;}
	else if (c < 0x10000)	 {first = 0xe0;len = 3;}
	else if (c < 0x200000) {first = 0xf0;len = 4;}
	else if (c < 0x4000000) {first = 0xf8;len = 5;}
	else {first = 0xfc;len = 6;}
	for (int i = len - 1; i > 0; --i)
	{
		outbuf[i] = (c & 0x3f) | 0x80;
		c >>= 6;
	}
	outbuf[0] = c | first;
	return len;
}

std::string convertToUtf8(const char* chars, int len)  // 1251 -> utf8
{
	if (len <= 0 || chars == NULL) return std::string();
	const unsigned char * c = (const unsigned char *)chars;
	char *uc = new char[len * 4 + 1], *p = uc;
	for (int i = 0; i < len; i++)
	{
		unsigned short unicode = c[i] < 127 ? c[i] : win_cp1251[c[i] - 128];
		p += utf16le_to_utf8(unicode, p);
	}
	*p = 0;
	std::string r = uc;
	delete [] uc;
	return r;
}

std::string get_string(std::vector<unsigned char> &source, int *position)
{
	std::string result;
	int src_size = source.size();
	if (*position < 0 || *position >= src_size - 1)
	{
		fprintf(stderr, "Error: get_string: incorrect position (%i of %i)",  *position, src_size);
		return result;
	}
	const unsigned char* buf = source.data();
	unsigned short uchar = 0;
	char tmp[6];
	while (*position < src_size - 1)
	{
		uchar = *(unsigned short*)(buf + *position);
		*position += 2;
		if (!uchar) break;
		int char_len = utf16le_to_utf8(uchar, tmp);
		result.append(tmp, char_len);
	}
	return result;
}

void append_string(std::string &source, std::vector<unsigned char> &destination)
{
	std::vector<unsigned char> input(source.begin(), source.end());
	for(int i = 0; i < (int)input.size();)
	{
		unsigned short ch = 0;
		if((input[i] & 0xE0) == 0xE0)
		{
			ch = ((input[i] & 0x0F) << 12) | (
					(input[i+1] & 0x3F) << 6)
				  | (input[i+2] & 0x3F);
			i += 3;
		}
		else if((input[i] & 0xC0) == 0xC0)
		{
			ch = ((input[i] & 0x1F) << 6) | (input[i+1] & 0x3F);
			i += 2;
		}
		else if(input[i] < 0x80)
		{
			ch = input[i];
			i += 1;
		}
		destination.push_back(ch & 0xFF);
		destination.push_back((ch & 0xFF00) >> 8);
	}
	destination.push_back(0);
	destination.push_back(0);
}

void append_int32(int source, std::vector<unsigned char> &destination)
{
	destination.push_back(source & 0xFF);
	destination.push_back((source >> 8) & 0xFF);
	destination.push_back((source >> 16) & 0xFF);
	destination.push_back((source >> 24) & 0xFF);
}

int get_int32(std::vector<unsigned char> &source, int *position)
{
	if ((size_t)(*position + 4) > source.size())
	{
		fprintf(stderr, "Error: get_int32: incorrect position (%i of %i)",  *position, source.size());
		return -1;
	}
	int result = 0;
	*position += 4;
	const unsigned char* buf = source.data();
	result = *(int*)(buf + *position);
	return result;
}

void str_replace(std::string &src, std::string substr, std::string res)
{
	size_t pos;
	while ((pos = src.find(substr)) != std::string::npos) src.replace(pos, substr.length(), res);
}

std::string trim(const std::string &a_str)
{
	std::string::size_type _from = a_str.find_first_not_of( " \t" ), from = (_from == std::string::npos ? 0 : _from);
	std::string::size_type to = a_str.find_last_not_of( " \t" );
	return a_str.substr( from, to == std::string::npos ? std::string::npos : to - from + 1 );
}

const char* timed_prefix(const char* prefix)
{
	time_t rawtime;
	struct tm* timeinfo;
	char buffer[32];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 32, "%Y-%m-%d %H:%M:%S: ", timeinfo);
	std::string res(buffer);
	if (prefix) res += prefix;
	return res.c_str();
}

void GpgCmd(std::vector<unsigned char> source, std::string command, std::vector<unsigned char> &out, std::string &log)
{
	int fd_input[2], fd_output[2], fd_pass[2], fd_err[2];
	pid_t pid;

	if ( (pipe(fd_input) < 0) || (pipe(fd_output) < 0) || (pipe(fd_pass) < 0) || (pipe(fd_err) < 0))
	{
		perror(timed_prefix("PIPE ERROR"));
		return;
	}
	if ( (pid = fork()) < 0 )
	{
		perror(timed_prefix("FORK ERROR"));
		return;
	}
	else  if (pid == 0)     // CHILD PROCESS
	{
		close(fd_input[1]); close(fd_output[0]);
		close(fd_pass[1]); close(fd_err[0]);
		dup2(fd_pass[0], 100); dup2(fd_err[1], 200);
		close(fd_pass[0]); close(fd_err[1]);

		if (fd_input[0] != STDIN_FILENO)
		{
			if (dup2(fd_input[0], STDIN_FILENO) != STDIN_FILENO)
			{
				perror(timed_prefix("dup2 error to stdin"));
			}
			close(fd_input[0]);
		}

		if (fd_output[1] != STDOUT_FILENO)
		{
			if (dup2(fd_output[1], STDOUT_FILENO) != STDOUT_FILENO)
			{
				perror(timed_prefix("dup2 error to stdout"));
			}
			close(fd_output[1]);
		}

		if (command.length() > 1023)
		{
			fprintf(stderr, "%sError: too long command", timed_prefix());
			_Exit(0);
		}

		char cmd_buf[1024]; memset (cmd_buf, 0, 1024);
		char *args[200]; memset (args, 0, 200);
		int arg_count = 0;
		bool arg_started = false;
		bool quotes_started = false;
		int i;
		strncpy(cmd_buf, command.c_str(), command.length());
		for (i = 0; cmd_buf[i] != '\0'; i++)
		{
			if (!arg_started)
			{
				if (strchr(" \t", cmd_buf[i])) continue;
				arg_started = true;
				if (cmd_buf[i] == '"') quotes_started = true;
				args[arg_count++] = &cmd_buf[quotes_started ? i + 1 : i];
				continue;
			}
			if (quotes_started && cmd_buf[i] != '"') continue;
			if (arg_started && !quotes_started && !strchr(" \t=\0", cmd_buf[i])) continue;
			if (quotes_started && cmd_buf[i] == '"')
			{
				arg_started = false;
				cmd_buf[i] = NULL;
				if (!strchr(" \t=\0", cmd_buf[i + 1])) break;
				quotes_started = false;
			}
			else if (arg_started)
			{
				arg_started = false;
				cmd_buf[i] = NULL;
			}
		}
		if (quotes_started)
		{
			fprintf(stderr, "%sError: invalid arguments (pos = %i)", timed_prefix(), i + 1);
			_Exit(0);
		}

		if (execvp(args[0], args) < 0) perror(timed_prefix("system error"));
		_Exit(0);
	}
	else        // PARENT PROCESS
	{
		int rv;
		close(fd_input[0]);
		close(fd_output[1]);
		close(fd_pass[0]);
		close(fd_err[1]);

		if (write(fd_input[1], source.data(), source.size()) != (int)source.size() )
		{
			perror(timed_prefix("WRITE ERROR TO PIPE"));
			close(fd_input[1]);
			close(fd_pass[1]);
			close(fd_output[0]);
			close(fd_err[0]);
			return;
		}
		close(fd_input[1]);

		std::string pass = conf->GetString("server_pwd", "") + "\n";
		if (write(fd_pass[1], pass.c_str(),pass.length()) != (ssize_t)pass.length())
		{
			perror(timed_prefix("WRITE ERROR TO PIPE"));
			close(fd_pass[1]);
			close(fd_output[0]);
			close(fd_err[0]);
			return;
		}
		close(fd_pass[1]);

		char res_buf[1024];
		while ((rv = read(fd_output[0], res_buf, 1024)) > 0)
		{
			std::vector<unsigned char> tmp_res(&res_buf[0], &res_buf[rv - 1]);
			out.insert(out.end(), tmp_res.begin(), tmp_res.end());
		}
		if (rv < 0)
		{
			perror(timed_prefix("READ ERROR FROM PIPE"));
			out.clear();
		}
		close(fd_output[0]);

		while ((rv = read(fd_err[0], res_buf, 1024)) > 0)
		{
			std::string tmp_res(&res_buf[0], &res_buf[rv - 1]);
			log.insert(log.end(), tmp_res.begin(), tmp_res.end());
		}
		if (rv < 0)
		{
			perror(timed_prefix("READ ERROR FROM PIPE"));
			log.clear();
		}
		close(fd_err[0]);
		//else if (rv == 0) perror("Child Closed Pipe");
	}
	return;
}

std::vector<unsigned char> Decrypt(int client_id, std::string message, char *key_id)
{
	std::vector<unsigned char> source(message.begin(), message.end());
	std::vector<unsigned char> result;
	std::string log;
	GpgCmd(source, "gpg -d --no-tty --passphrase-fd 100 --logger-fd 200", result, log);
	fprintf(stderr, "%s", timed_prefix(log.c_str()));
	char buf[32];
	sprintf(buf, "[%i]", client_id);
	if (!result.size()) fprintf(stderr, "%sError: %s", timed_prefix(), log.c_str());
	else if (log.length() > 0 && !strstr(log.c_str(), buf))
	{
		result.clear();
		fprintf(stderr, "%sError: Client ID doesn't found in signature data.", timed_prefix());
	}
	else if (log.length() > 0)
	{
		int pos = -1;
		while ((pos = log.find("ID ", pos + 1)) > 0)
		{
			pos += 3;
			int start = pos;
			while (log.length() > (unsigned char)(pos + 1) && strchr("0123456789abcdefABCDEFx", *(log.c_str() + pos))) pos++;
			if (*(log.c_str() + pos) == '\r' || *(log.c_str() + pos) == '\n')
			{
				if (pos - start > 16)
					fprintf(stderr, "%sError: Signature key ID too long", timed_prefix());
				else
					strncpy(key_id, log.c_str() + start, pos - start);
				break;
			}
		}
	}
	return result;
}

std::string Encrypt(char *key_id, std::vector<unsigned char> message)
{
	char buf[256];  // --force-mdc      --compress-algo 1 -z 6
	sprintf(buf, "gpg --pgp6 -u server2010 -sea -r %s --no-tty --yes --always-trust --passphrase-fd 100 --logger-fd 200", key_id);
	std::vector<unsigned char> result;
	std::string log;
	GpgCmd(message, buf, result, log);
	fprintf(stderr, "%s", timed_prefix(log.c_str()));
	return std::string(result.begin(), result.end());
}
