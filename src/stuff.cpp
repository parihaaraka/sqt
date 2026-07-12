#include "stuff.h"
#include <ctime>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
        fprintf(stderr, "Error: get_int32: incorrect position (%i of %zu)",  *position, source.size());
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
