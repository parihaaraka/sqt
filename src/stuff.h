#ifndef STUFF_H
#define STUFF_H

#include <string>
#include <vector>

int utf16le_to_utf8(unsigned int c, char *outbuf);

std::string convertToUtf8(const char* chars, int len);

void append_string(std::string &source, std::vector<unsigned char> &destination);

std::string get_string(std::vector<unsigned char> &source, int *position);

void append_int32(int source, std::vector<unsigned char> &destination);

int get_int32(std::vector<unsigned char> &source, int *position);

void str_replace(std::string &src, std::string substr, std::string res);

std::string trim(const std::string &a_str);

const char* timed_prefix(const char* prefix = NULL);

void GpgCmd(std::vector<unsigned char> source, std::string command, std::vector<unsigned char> &out, std::string &log);

std::vector<unsigned char> Decrypt(int client_id, std::string message, char *key_id);

std::string Encrypt(char *key_id, std::vector<unsigned char> message);

#endif // STUFF_H
