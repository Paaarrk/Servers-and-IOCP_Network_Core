#ifndef __TEXT_PARSER_H__
#define __TEXT_PARSER_H__
#define BUFF_LEN	256
#define MAX_STRLEN	256

enum TokenType{
	NONE = 0,
	NAMESPACE,
	NAMESPACE_BLOCK,
	KEY,
	VALUE_INPUT,
	VALUE_INT,
	VALUE_DOUBLE,
	STR_BLOCK,
	ARR_BLOCK,
	STRUCT_BLOCK,
	COMMENT_LINE,
	COMMENT_BLOCK,
	LITERAL
};
struct Token
{
	int type;
	char* tokenStart;
	int tokenLen;
	int startline;
	int endline;
};
class CParser
{
private:
	static char errbuffer[128];
	const char* _path;
	size_t _bufflen;
	char* _buffer;

	size_t _scopelen;
	char* _scopestart;
	int _scopeline;
	CParser(const CParser& ref) = delete;
	CParser& operator=(const CParser& ref) = delete;

	void getNextToken(Token* now, Token* get, bool isScopeIn = false, bool setTokenType = true);
	void setTokenType(Token* now);
	void setTokenTypeSingle(Token* now);
	void ScopeClear();

public:
	CParser(const char* path) : _path(path), _bufflen(0), _buffer(nullptr), _scopelen(0), _scopestart(nullptr), _scopeline(1){}
	~CParser()
	{
		if (_buffer != nullptr)
			free(_buffer);
	}
	bool LoadFile();

	void GetValue(const char* Namespace, const char* Key, char* value);
	void GetValue(const char* Namespace, const char* Key, int* value);
	void GetValue(const char* Namespace, const char* Key, double* value);
	void GetValue(const char* Namespace, const char* StructName, const char* Key, char* value);
	void GetValue(const char* Namespace, const char* StructName, const char* Key, int* value);
	void GetValue(const char* Namespace, const char* StructName, const char* Key, double* value);
	void GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, char* value);
	void GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, int* value);
	void GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, double* value);
};

#endif