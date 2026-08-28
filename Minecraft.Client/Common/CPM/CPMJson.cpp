#include "stdafx.h"
#include "CPMJson.h"
#include <stdlib.h>
#include <string.h>

// A project's element tree is only a few levels deep in practice; this is a
// guard against hostile input, not a real limit.
#define CPM_JSON_MAX_DEPTH 64
#define CPM_JSON_MAX_NODES 400000

int CPMJson::newNode(int t)
{
	if ((int)nodes.size() >= CPM_JSON_MAX_NODES)
	{
		if (error.empty()) error = "json too large";
		return -1;
	}
	Node n;
	n.type = t;
	n.num = 0;
	n.boolean = false;
	nodes.push_back(n);
	return (int)nodes.size() - 1;
}

void CPMJson::skipWs()
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
}

bool CPMJson::parse(const char *data, int len)
{
	nodes.clear();
	error.clear();
	root = -1;

	if (data == NULL || len <= 0) { error = "empty json"; return false; }

	// Tolerate a UTF-8 BOM; some editors write one.
	if (len >= 3 && (unsigned char)data[0] == 0xEF &&
	    (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
	{
		data += 3;
		len -= 3;
	}

	p = data;
	end = data + len;
	depth = 0;

	skipWs();
	root = parseValue();
	if (root < 0)
	{
		if (error.empty()) error = "malformed json";
		return false;
	}
	return true;
}

int CPMJson::parseValue()
{
	if (depth > CPM_JSON_MAX_DEPTH) { error = "json nested too deeply"; return -1; }
	skipWs();
	if (p >= end) { error = "unexpected end of json"; return -1; }

	switch (*p)
	{
	case '{': return parseObject();
	case '[': return parseArray();
	case '"': return parseString();

	case 't':
		if (end - p >= 4 && memcmp(p, "true", 4) == 0)
		{
			p += 4;
			int n = newNode(CPM_JSON_BOOL);
			if (n >= 0) nodes[n].boolean = true;
			return n;
		}
		error = "bad literal";
		return -1;

	case 'f':
		if (end - p >= 5 && memcmp(p, "false", 5) == 0)
		{
			p += 5;
			int n = newNode(CPM_JSON_BOOL);
			if (n >= 0) nodes[n].boolean = false;
			return n;
		}
		error = "bad literal";
		return -1;

	case 'n':
		if (end - p >= 4 && memcmp(p, "null", 4) == 0)
		{
			p += 4;
			return newNode(CPM_JSON_NULL);
		}
		error = "bad literal";
		return -1;

	default:
		return parseNumber();
	}
}

int CPMJson::parseString()
{
	if (p >= end || *p != '"') { error = "expected string"; return -1; }
	p++;

	std::string s;
	while (p < end && *p != '"')
	{
		if (*p == '\\')
		{
			p++;
			if (p >= end) { error = "unterminated escape"; return -1; }
			switch (*p)
			{
			case 'n': s += '\n'; break;
			case 't': s += '\t'; break;
			case 'r': s += '\r'; break;
			case 'b': s += '\b'; break;
			case 'f': s += '\f'; break;
			case '/': s += '/';  break;
			case '"': s += '"';  break;
			case '\\': s += '\\'; break;
			case 'u':
			{
				// Only the BMP is handled, and only as UTF-8. Project files use
				// escapes for names, which never affect the geometry.
				if (end - p < 5) { error = "bad unicode escape"; return -1; }
				char hex[5];
				memcpy(hex, p + 1, 4);
				hex[4] = 0;
				unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
				p += 4;
				if (cp < 0x80) s += (char)cp;
				else if (cp < 0x800)
				{
					s += (char)(0xC0 | (cp >> 6));
					s += (char)(0x80 | (cp & 0x3F));
				}
				else
				{
					s += (char)(0xE0 | (cp >> 12));
					s += (char)(0x80 | ((cp >> 6) & 0x3F));
					s += (char)(0x80 | (cp & 0x3F));
				}
				break;
			}
			default:
				error = "bad escape";
				return -1;
			}
			p++;
		}
		else
		{
			s += *p++;
		}
	}

	if (p >= end) { error = "unterminated string"; return -1; }
	p++;   // closing quote

	int n = newNode(CPM_JSON_STRING);
	if (n >= 0) nodes[n].str = s;
	return n;
}

int CPMJson::parseNumber()
{
	const char *start = p;
	if (p < end && (*p == '-' || *p == '+')) p++;
	while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' ||
	                   *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) p++;

	if (p == start) { error = "expected value"; return -1; }

	std::string t(start, p);
	int n = newNode(CPM_JSON_NUMBER);
	if (n >= 0) nodes[n].num = atof(t.c_str());
	return n;
}

int CPMJson::parseObject()
{
	int self = newNode(CPM_JSON_OBJECT);
	if (self < 0) return -1;
	p++;   // '{'
	depth++;

	skipWs();
	if (p < end && *p == '}') { p++; depth--; return self; }

	while (p < end)
	{
		skipWs();

		// Read the key inline rather than as a node, so the pool holds only
		// values and a member is just a value node carrying its key.
		std::string key;
		{
			int k = parseString();
			if (k < 0) { depth--; return -1; }
			key.swap(nodes[k].str);
			// The key node was scratch; drop it if it is still the last one.
			if (k == (int)nodes.size() - 1) nodes.pop_back();
		}

		skipWs();
		if (p >= end || *p != ':') { error = "expected ':'"; depth--; return -1; }
		p++;

		int val = parseValue();
		if (val < 0) { depth--; return -1; }

		nodes[val].key = key;
		nodes[self].children.push_back(val);

		skipWs();
		if (p < end && *p == ',') { p++; continue; }
		if (p < end && *p == '}') { p++; depth--; return self; }

		error = "expected ',' or '}'";
		depth--;
		return -1;
	}

	error = "unterminated object";
	depth--;
	return -1;
}

int CPMJson::parseArray()
{
	int self = newNode(CPM_JSON_ARRAY);
	if (self < 0) return -1;
	p++;   // '['
	depth++;

	skipWs();
	if (p < end && *p == ']') { p++; depth--; return self; }

	while (p < end)
	{
		int val = parseValue();
		if (val < 0) { depth--; return -1; }
		nodes[self].children.push_back(val);

		skipWs();
		if (p < end && *p == ',') { p++; continue; }
		if (p < end && *p == ']') { p++; depth--; return self; }

		error = "expected ',' or ']'";
		depth--;
		return -1;
	}

	error = "unterminated array";
	depth--;
	return -1;
}

//////////////////////////////////////////////////////////////////////////
// Lookups
//////////////////////////////////////////////////////////////////////////

int CPMJson::member(int obj, const char *key) const
{
	if (obj < 0 || obj >= (int)nodes.size()) return -1;
	const Node &n = nodes[obj];
	if (n.type != CPM_JSON_OBJECT) return -1;

	for (size_t i = 0; i < n.children.size(); i++)
	{
		int c = n.children[i];
		if (nodes[c].key.compare(key) == 0) return c;
	}
	return -1;
}

std::string CPMJson::getStr(int obj, const char *key, const char *def) const
{
	int n = member(obj, key);
	if (n < 0 || nodes[n].type != CPM_JSON_STRING) return std::string(def ? def : "");
	return nodes[n].str;
}

int CPMJson::count(int node) const
{
	if (node < 0 || node >= (int)nodes.size()) return 0;
	return (int)nodes[node].children.size();
}

int CPMJson::at(int arr, int index) const
{
	if (arr < 0 || arr >= (int)nodes.size()) return -1;
	const Node &n = nodes[arr];
	if (index < 0 || index >= (int)n.children.size()) return -1;
	return n.children[index];
}

bool CPMJson::getBool(int obj, const char *key, bool def) const
{
	int n = member(obj, key);
	if (n < 0) return def;
	if (nodes[n].type == CPM_JSON_BOOL) return nodes[n].boolean;
	if (nodes[n].type == CPM_JSON_NUMBER) return nodes[n].num != 0;
	return def;
}

double CPMJson::getNum(int obj, const char *key, double def) const
{
	int n = member(obj, key);
	if (n < 0) return def;
	if (nodes[n].type == CPM_JSON_NUMBER) return nodes[n].num;
	return def;
}

void CPMJson::getVec3(int obj, const char *key, float &x, float &y, float &z,
                      float dx, float dy, float dz) const
{
	x = dx; y = dy; z = dz;
	int v = member(obj, key);
	if (v < 0 || nodes[v].type != CPM_JSON_OBJECT) return;
	x = (float)getNum(v, "x", dx);
	y = (float)getNum(v, "y", dy);
	z = (float)getNum(v, "z", dz);
}
