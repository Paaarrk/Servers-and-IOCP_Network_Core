#include <iostream>
#include <windows.h>
#include "21_TextParser.h"
void SkipUTF8BOM(FILE* fp, size_t* size)
{
	fseek(fp, 0, SEEK_SET);
	unsigned char bom[3];
	size_t n = fread(bom, 1, 3, fp);

	// EF BB BF → UTF-8 BOM
	if (n == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
	{
		*size -= 3;
		return; // 그냥 그대로 (이미 3바이트 읽혀서 포인터 이동됨)
	}
	else
	{
		// BOM이 아니면 파일 포인터를 처음으로 되돌림
		fseek(fp, 0, SEEK_SET);
	}
}
char CParser::errbuffer[128];
bool CParser::LoadFile()
{
	FILE* file = nullptr;
	long size;
	if (fopen_s(&file, _path, "rb"))
		throw std::invalid_argument("File is not exist");

	fseek(file, 0, SEEK_END);
	size = ftell(file);
	if (size == -1)
		throw std::invalid_argument("ftell failed. Please check File can be seekable");
	_bufflen = (size_t)size;

	SkipUTF8BOM(file, &_bufflen);

	if (_bufflen == 0)
		throw std::invalid_argument("File is Empty");
	_buffer = (char*)malloc(sizeof(char) * (_bufflen + 1));
	if (_buffer == nullptr)
		throw std::invalid_argument("Buffer malloc failed");
	if (!fread(_buffer, _bufflen, 1, file))
		throw std::invalid_argument("File read failed");
	_buffer[_bufflen] = '\0';

	_scopestart = _buffer;
	_scopelen = _bufflen;

	fclose(file);

	return true;
}


void CParser::getNextToken(Token* now, Token* get, bool isScopeIn, bool setTokentype)
{
	char* start;
	size_t cnt;
	int line;

	if (now == nullptr)	//스코프 유지
	{
		if (_scopelen == 0)
		{
			_scopelen = _bufflen;
			_scopestart = _buffer;
			_scopeline = 1;
		}
		start = _scopestart;
		cnt = _scopelen;
		line = _scopeline;
	}
	else if(isScopeIn)	//토큰 내부
	{
		_scopestart = now->tokenStart + 1;	// '[', '{', '(' 를 뺌
		_scopelen = now->tokenLen - 1;		// ']', '}', ')' 를 뺌
		_scopeline = now->startline;
		start = _scopestart;
		cnt = _scopelen;
		line = _scopeline;
	}
	else				//현재 스코프 유지
	{
		start = (now->tokenStart) + (now->tokenLen);
		cnt = _scopelen - (size_t)(now->tokenStart - _scopestart) - now->tokenLen;
		line = now->endline;
	}

	bool startcheck = false;
	bool endcheck = false;

	bool skip = false;
	char skipend = 0;

	for (size_t i = 0; i <= cnt; i++)
	{
		if (skip)
		{
			if (start[i] == skipend)
			{
				if (start[i] == '*')
				{
					if (start[i + 1] == '/')
					{
						startcheck = true;
						endcheck = true;
						get->tokenLen = (int)(&start[i] - (get->tokenStart)) + 2;
						break;
					}
				}
				else if (start[i] == '\n')
				{
					startcheck = true;
					endcheck = true;
					if (start[i - 1] == '\r')
						get->tokenLen = (int)(&start[i] - (get->tokenStart)) - 1;
					else
						get->tokenLen = (int)(&start[i] - (get->tokenStart));
					break;
				}
				else
				{
					startcheck = true;
					endcheck = true;
					get->tokenLen = (int)(&start[i] - (get->tokenStart)) + 1;
					if (skipend == '"' && get->tokenLen > MAX_STRLEN)
					{
						startcheck = false;
						endcheck = false;
						sprintf_s(errbuffer, "[%s] Cannot find '\"' for this string or this is too long. Please extended MAX_STRLEN, line %d", _path, now->startline);
						throw std::invalid_argument(errbuffer);
					}
					break;
				}
			}
			else
			{	// { [ ( 가 한번 더나오는지 체크
				if (skipend == '}' && start[i] == '{')
				{
					startcheck = false;
					endcheck = false;
					sprintf_s(errbuffer, "[%s] Cannot find '}' for this namespace, line %d", _path, now->startline);
					throw std::invalid_argument(errbuffer);
				}
				else if (skipend == ')' && start[i] == '(')
				{
					startcheck = false;
					endcheck = false;
					sprintf_s(errbuffer, "[%s] Cannot find ')' for this namespace, line %d", _path, now->startline);
					throw std::invalid_argument(errbuffer);
				}
				else if (skipend == ']' && start[i] == '[')
				{
					startcheck = false;
					endcheck = false;
					sprintf_s(errbuffer, "[%s] Cannot find ']' for this namespace, line %d", _path, now->startline);
					throw std::invalid_argument(errbuffer);
				}
			}
		}
		else
		{
			if (start[i] != '\n' && start[i] != '\r' &&
				start[i] != '\t' && start[i] != ' ' && start[i] != '\0' &&
				start[i] != ')' && start[i] != ']' && start[i] != '}')	//이건 skip에서 체크하는 것들
			{
				if (startcheck == false)
				{	// 토큰의 시작점 발견
					startcheck = true;

					// 스킵 여부 결정
					if (start[i] == '{')
					{
						skip = true;
						skipend = '}';
						get->tokenStart = (char*)&start[i];
						get->startline = line;
					}
					else if (start[i] == '/')
					{
						if (start[i + 1] == '*')
						{
							skip = true;
							skipend = '*';
							get->tokenStart = (char*)&start[i];
							get->startline = line;
						}
						else if (start[i + 1] == '/')
						{
							skip = true;
							skipend = '\n';
							get->tokenStart = (char*)&start[i];
							get->startline = line;
						}
					}
					else if (start[i] == '[')
					{
						skip = true;
						skipend = ']';
						get->tokenStart = (char*)&start[i];
						get->startline = line;
					}
					else if (start[i] == '"')
					{
						skip = true;
						skipend = '"';
						get->tokenStart = (char*)&start[i];
						get->startline = line;
					}
					else if (start[i] == '(')
					{
						skip = true;
						skipend = ')';
						get->tokenStart = (char*)&start[i];
						get->startline = line;
					}
					else if (start[i] == '=')
					{
						get->tokenStart = (char*)&start[i];
						get->startline = line;
						endcheck = true;
						get->tokenLen = 1;
						break;
					}
					else
					{	// 그냥 문자열일 경우
						get->tokenStart = (char*)&start[i];
						get->startline = line;
					}
				}
				else
				{
					if (start[i] == '=')
					{
						// 토큰의 끝
						endcheck = true;
						get->tokenLen = (int)(&start[i] - (get->tokenStart));
						break;
					}
				}
			}
			else
			{
				if (startcheck == true)
				{
					// 토큰의 끝
					endcheck = true;
					get->tokenLen = (int)(&start[i] - (get->tokenStart));
					break;
				}
			}
		}


		if (start[i] == '\n')
			line++;
	}

	if (startcheck == false || endcheck == false)
	{
		get->tokenLen = 0;
	}
	get->endline = line;
	if(setTokentype)
		setTokenType(get);
}
void CParser::setTokenTypeSingle(Token* now)
{
	if (now->tokenLen == 0)
	{
		now->type = TokenType::NONE;
		return;	// 비정상 토큰
	}

	if ((*(now->tokenStart) == '/') && (*(now->tokenStart + 1) == '/'))
		(now->type) = TokenType::COMMENT_LINE;
	else if ((*(now->tokenStart) == '/') && (*(now->tokenStart + 1) == '*'))
		(now->type) = TokenType::COMMENT_BLOCK;
	else if ((*(now->tokenStart) == '{') && (*(now->tokenStart + now->tokenLen - 1) == '}'))
		(now->type) = TokenType::NAMESPACE_BLOCK;
	else if ((*(now->tokenStart) == '[') && (*(now->tokenStart + now->tokenLen - 1) == ']'))
		(now->type) = TokenType::ARR_BLOCK;
	else if ((*(now->tokenStart) == '(') && (*(now->tokenStart + now->tokenLen - 1) == ')'))
		(now->type) = TokenType::STRUCT_BLOCK;
	else if ((*(now->tokenStart) == '"') && (*(now->tokenStart + now->tokenLen - 1) == '"'))
		(now->type) = TokenType::STR_BLOCK;
	else if ((*(now->tokenStart) == '=') && ((now->tokenLen) == 1))
		(now->type) = TokenType::VALUE_INPUT;
	else			
		(now->type) = TokenType::LITERAL;


}
void CParser::setTokenType(Token* now)
{
	if (now->tokenLen == 0)
	{
		now->type = TokenType::NONE;
		return;
	}

	Token nextToken;

	if ((*(now->tokenStart) == '/') && (*(now->tokenStart + 1) == '/'))
		(now->type) = TokenType::COMMENT_LINE;
	else if ((*(now->tokenStart) == '/') && (*(now->tokenStart + 1) == '*'))
		(now->type) = TokenType::COMMENT_BLOCK;
	else if ((*(now->tokenStart) == '{') && (*(now->tokenStart + now->tokenLen - 1) == '}'))
		(now->type) = TokenType::NAMESPACE_BLOCK;
	else if ((*(now->tokenStart) == '[') && (*(now->tokenStart + now->tokenLen - 1) == ']'))
		(now->type) = TokenType::ARR_BLOCK;
	else if ((*(now->tokenStart) == '(') && (*(now->tokenStart + now->tokenLen - 1) == ')'))
		(now->type) = TokenType::STRUCT_BLOCK;
	else if ((*(now->tokenStart) == '"') && (*(now->tokenStart + now->tokenLen - 1) == '"'))
		(now->type) = TokenType::STR_BLOCK;
	else if ((*(now->tokenStart) == '=') && (now->tokenLen == 1))
		(now->type) = TokenType::VALUE_INPUT;
	else
	{
		// 한번에 판단 안되는 경우 다음 토큰 검색
		getNextToken(now, &nextToken, false, false);
		setTokenTypeSingle(&nextToken);
		char* ptr;
		Token befToken;
		while (nextToken.type == TokenType::COMMENT_BLOCK ||
			nextToken.type == TokenType::COMMENT_LINE)
		{
			getNextToken(&nextToken, &nextToken, false, false);
			setTokenTypeSingle(&nextToken);
		}
		int num_num = 0;
		int dot_num = 0;
		switch (nextToken.type)
		{
		case TokenType::NONE:	//정말 끝이야 (자기 다음 머 없어)
			for (int i = 0; i < now->tokenLen; i++)
			{
				if ((unsigned char)((now->tokenStart)[i]) >= (unsigned char)'0' && (unsigned char)((now->tokenStart)[i]) <= (unsigned char)'9')
				{
					num_num++;
				}
				else if ((now->tokenStart)[i] = '.')
				{
					dot_num++;
				}
				if (num_num == now->tokenLen)
					now->type = TokenType::VALUE_INT;
				else if (num_num == now->tokenLen - 1 && dot_num == 1)
					now->type = TokenType::VALUE_DOUBLE;
				else
					now->type = TokenType::NONE;
			}
			break;
		case TokenType::NAMESPACE_BLOCK:
			now->type = TokenType::NAMESPACE;
			break;
		case TokenType::VALUE_INPUT:
			now->type = TokenType::KEY;
			break;
		case TokenType::LITERAL:
			// 다음도 결정이 불가능해? value아니면 문법오류
			// 앞에 '='를 찾음
			for (ptr = now->tokenStart; ptr != _buffer; ptr--)
			{
				if (*ptr == '=')
					break;
			}

			if (ptr == _buffer)
			{	//없는거 -> 그냥 값만 덩그라니
				now->type = TokenType::NONE;
			}
			else
			{	//찾은 것의 다음 토큰이 나자신인지 확인
				ptr++;	// = 의 다음으로 시작점 잡기
				befToken.tokenStart = ptr;
				befToken.tokenLen = 1;
				befToken.startline = 0;	//여기서 line이 중요하지 않음. 대충 넣은 값
				befToken.endline = 0;
				getNextToken(&befToken, &befToken, false, false);	//=가 없어서 스코프가 정상이 아닌곳에서 = 를 찾았으면 값도 당연 비정상
				setTokenTypeSingle(&befToken);
				while (befToken.type == TokenType::COMMENT_BLOCK || befToken.type == TokenType::COMMENT_LINE)
				{
					getNextToken(&befToken, &befToken, false, false);
					setTokenTypeSingle(&befToken);
				}

				if (befToken.tokenLen == 0)
				{
					//여기 걸리면 말이 안됨
				}
				else
				{
					if (befToken.tokenStart == now->tokenStart)
					{	// '=' '나자신' 이므로 Value
						for (int i = 0; i < now->tokenLen; i++)
						{
							if (((unsigned char)(now->tokenStart)[i]) >= (unsigned char)'0' && ((now->tokenStart)[i]) <= (unsigned char)'9')
							{
								num_num++;
							}
							else if ((now->tokenStart)[i] = '.')
							{
								dot_num++;
							}
							if (num_num == now->tokenLen)
								now->type = TokenType::VALUE_INT;
							else if (num_num == now->tokenLen - 1 && dot_num == 1)
								now->type = TokenType::VALUE_DOUBLE;
							else
								now->type = TokenType::NONE;
						}
					}
					else
					{	// '=' 'Value' '나자신' 이므로 문법오류
						for (int i = 0; i < now->tokenLen; i++)
						{
							if ((unsigned char)((now->tokenStart)[i]) >= (unsigned char)'0' && (unsigned char)((now->tokenStart)[i]) <= (unsigned char)'9')
							{
								num_num++;
							}
							else if ((now->tokenStart)[i] = '.')
							{
								dot_num++;
							}
							if (num_num == now->tokenLen)
								now->type = TokenType::VALUE_INT;
							else if (num_num == now->tokenLen - 1 && dot_num == 1)
								now->type = TokenType::VALUE_DOUBLE;
							else
								now->type = TokenType::NONE;
						}
					}
				}

			}

			break;
		}
	}


}
void CParser::ScopeClear()
{
	_scopelen = _bufflen;
	_scopestart = _buffer;
	_scopeline = 1;
}

void CParser::GetValue(const char* Namespace, const char* Key, char* value)
{
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s], Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else 
		namespaceblockfind = true;
	
	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
	
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 값 - 바로 다음에 처음으로 나오는 값
	char valueBuffer[BUFF_LEN];
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::STR_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not String : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	// "  " 에서 " 빼기
	if (memcpy_s(valueBuffer, BUFF_LEN, temp_value.tokenStart + 1, temp_value.tokenLen - 2))
	{
		sprintf_s(errbuffer, "[%s] BUFF_LEN is too short", _path);
		throw std::invalid_argument(errbuffer);
	}
	valueBuffer[temp_value.tokenLen - 2] = '\0';

	if (value == nullptr)
	{
		sprintf_s(errbuffer, "[%s] value buffer is null", _path);
		throw std::invalid_argument(errbuffer);
	}
	if (strcpy_s(value, strlen(valueBuffer) + 1, valueBuffer))
	{
		sprintf_s(errbuffer, "[%s] value buffer is too short please use 256", _path);
		throw std::invalid_argument(errbuffer);
	}


	ScopeClear();
}
void CParser::GetValue(const char* Namespace, const char* Key, int* value)

{
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path ,temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 값 - 바로 다음에 처음으로 나오는 값
	char valueBuffer[BUFF_LEN];
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::VALUE_INT)
	{
		sprintf_s(errbuffer, "[%s] This is not int : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (memcpy_s(valueBuffer, BUFF_LEN, temp_value.tokenStart, temp_value.tokenLen))
	{
		sprintf_s(errbuffer, "[%s] BUFF_LEN is too short", _path);
		throw std::invalid_argument(errbuffer);
	}
	valueBuffer[temp_value.tokenLen] = '\0';

	if (value == nullptr)
	{
		sprintf_s(errbuffer, "[%s] value buffer is null", _path);
		throw std::invalid_argument(errbuffer);
	}

	*value = atoi(valueBuffer);


	ScopeClear();
}
void CParser::GetValue(const char* Namespace, const char* Key, double* value)

{
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, Key, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 값 - 바로 다음에 처음으로 나오는 값
	char valueBuffer[BUFF_LEN];
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::VALUE_DOUBLE)
	{
		sprintf_s(errbuffer, "[%s] This is not DOUBLE : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (memcpy_s(valueBuffer, BUFF_LEN, temp_value.tokenStart, temp_value.tokenLen))
	{
		sprintf_s(errbuffer, "[%s] BUFF_LEN is too short", _path );
		throw std::invalid_argument(errbuffer);
	}
	valueBuffer[temp_value.tokenLen + 1] = '\0';

	if (value == nullptr)
	{
		sprintf_s(errbuffer, "[%s] value buffer is null", _path);
		throw std::invalid_argument(errbuffer);
	}

	*value = atof(valueBuffer);


	ScopeClear();
}

void CParser::GetValue(const char* Namespace, const char* StructName, const char* Key, char* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region structname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "Cannot find Key or SyntaxError : line [%d, %d]", temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 값 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::STRUCT_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = temp_value.tokenLen - 1;
	_scopestart = temp_value.tokenStart + 1;
	_scopeline = temp_value.startline;
	GetValue(nullptr, Key, value);
	ScopeClear();
}
void CParser::GetValue(const char* Namespace, const char* StructName, const char* Key, int* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region structname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 스트럭트 블럭 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::STRUCT_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = temp_value.tokenLen - 1;
	_scopestart = temp_value.tokenStart + 1;
	_scopeline = temp_value.startline;
	GetValue(nullptr, Key, value);
	ScopeClear();
}
void CParser::GetValue(const char* Namespace, const char* StructName, const char* Key, double* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region structname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, StructName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 스트럭트 블럭 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::STRUCT_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = temp_value.tokenLen - 1;
	_scopestart = temp_value.tokenStart + 1;
	_scopeline = temp_value.startline;
	GetValue(nullptr, Key, value);
	ScopeClear();
}

void CParser::GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, char* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region arrayname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 스트럭트 블럭 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::ARR_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region getStructNumI
	Token struct_value;
	bool getInumStruct = false;
	int cnt = 0;
	getNextToken(&temp_value, &struct_value, true);
	while (struct_value.type != TokenType::NONE)
	{
		if (struct_value.type != TokenType::STRUCT_BLOCK)
		{
			break;
		}
		else
		{
			if (cnt == i)
			{
				getInumStruct = true;
				break;
			}
			else
			{
				cnt++;
			}
		}
		getNextToken(&struct_value, &struct_value);
	}
	if (getInumStruct == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, struct_value.startline, struct_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = struct_value.tokenLen - 1;
	_scopestart = struct_value.tokenStart + 1;
	_scopeline = struct_value.startline;
	GetValue(nullptr, Key, value);
}

void CParser::GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, int* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region arrayname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 스트럭트 블럭 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::ARR_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region getStructNumI
	Token struct_value;
	bool getInumStruct = false;
	int cnt = 0;
	getNextToken(&temp_value, &struct_value, true);
	while (struct_value.type != TokenType::NONE)
	{
		if (struct_value.type != TokenType::STRUCT_BLOCK)
		{
			break;
		}
		else
		{
			if (cnt == i)
			{
				getInumStruct = true;
				break;
			}
			else
			{
				cnt++;
			}
		}
		getNextToken(&struct_value, &struct_value);
	}
	if (getInumStruct == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, struct_value.startline, struct_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = struct_value.tokenLen - 1;
	_scopestart = struct_value.tokenStart + 1;
	_scopeline = struct_value.startline;
	GetValue(nullptr, Key, value);
}

void CParser::GetValue(const char* Namespace, const char* ArrayName, int i, const char* Key, double* value)
{
#pragma region namespace
	bool namespacefind = false;
	Token temp_namespace;
	if (Namespace != nullptr)
	{
		getNextToken(nullptr, &temp_namespace);
		while (temp_namespace.type != TokenType::NONE)
		{
			if (temp_namespace.type == TokenType::NAMESPACE)
			{
				if (!memcmp(Namespace, temp_namespace.tokenStart, temp_namespace.tokenLen))
				{	// 찾음
					namespacefind = true;
					break;
				}
			}

			getNextToken(&temp_namespace, &temp_namespace);
		}
		if (temp_namespace.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_namespace.startline, temp_namespace.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespacefind = true;
	if (namespacefind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Namespace or SyntaxError : line [%d, %d] ", _path, temp_namespace.startline, temp_namespace.endline);
		throw std::invalid_argument(errbuffer);
	}

	Token namespace_block;
	bool namespaceblockfind = false;
	if (Namespace != nullptr)
	{
		getNextToken(&temp_namespace, &namespace_block);
		while (namespace_block.type != TokenType::NONE)
		{
			if (namespace_block.type == TokenType::NAMESPACE_BLOCK)
			{
				namespaceblockfind = true;
				break;
			}
			getNextToken(&namespace_block, &namespace_block);
		}
		if (namespace_block.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, namespace_block.startline, namespace_block.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
		namespaceblockfind = true;

	if (namespaceblockfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find NamespaceBlock or SyntaxError : line [%d, %d]", _path, namespace_block.startline, namespace_block.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region arrayname
	// 키
	Token temp_key;
	bool keyfind = false;
	if (Namespace == nullptr)
	{
		getNextToken(nullptr, &temp_key);
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	else
	{
		getNextToken(&namespace_block, &temp_key, true);	//블럭 내부로 입장
		while (temp_key.type != TokenType::NONE)
		{
			if (temp_key.type == TokenType::KEY)
			{
				if (!memcmp(temp_key.tokenStart, ArrayName, temp_key.tokenLen))
				{
					keyfind = true;
					break;
				}
			}
			getNextToken(&temp_key, &temp_key);
		}
		if (temp_key.type == TokenType::NONE)
		{
			sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_key.startline, temp_key.endline);
			throw std::invalid_argument(errbuffer);
		}
	}
	if (keyfind == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Key or SyntaxError : line [%d, %d]", _path, temp_key.startline, temp_key.endline);
		throw std::invalid_argument(errbuffer);
	}

	// 스트럭트 블럭 찾기 - 바로 다음에 처음으로 나오는 값
	Token temp_value;
	bool value_find = false;
	getNextToken(&temp_key, &temp_value);
	while (temp_value.type != TokenType::NONE)
	{
		if (temp_value.type == TokenType::STR_BLOCK || temp_value.type == TokenType::ARR_BLOCK
			|| temp_value.type == TokenType::STRUCT_BLOCK || temp_value.type == TokenType::VALUE_INT
			|| temp_value.type == TokenType::VALUE_DOUBLE)
		{
			value_find = true;
			break;
		}
		getNextToken(&temp_value, &temp_value);
	}
	if (temp_value.type == TokenType::NONE)
	{
		sprintf_s(errbuffer, "[%s] Invalid Token: line[%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}

	if (value_find == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
	if (temp_value.type != TokenType::ARR_BLOCK)
	{
		sprintf_s(errbuffer, "[%s] This is not STRUCT : line [%d, %d]", _path, temp_value.startline, temp_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion

#pragma region getStructNumI
	Token struct_value;
	bool getInumStruct = false;
	int cnt = 0;
	getNextToken(&temp_value, &struct_value, true);
	while (struct_value.type != TokenType::NONE)
	{
		if (struct_value.type != TokenType::STRUCT_BLOCK)
		{
			break;
		}
		else
		{
			if (cnt == i)
			{
				getInumStruct = true;
				break;
			}
			else
			{
				cnt++;
			}
		}
		getNextToken(&struct_value, &struct_value);
	}
	if (getInumStruct == false)
	{
		sprintf_s(errbuffer, "[%s] Cannot find Value or SyntaxError : line [%d, %d]", _path, struct_value.startline, struct_value.endline);
		throw std::invalid_argument(errbuffer);
	}
#pragma endregion
	ScopeClear();
	_scopelen = struct_value.tokenLen - 1;
	_scopestart = struct_value.tokenStart + 1;
	_scopeline = struct_value.startline;
	GetValue(nullptr, Key, value);
}