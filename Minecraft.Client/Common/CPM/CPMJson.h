#pragma once
// CPM - a small read-only JSON parser, for .cpmproject config files.
//
// Deliberately minimal: enough to read the editor's project format and nothing
// more. Values are kept in one flat node pool with child indices, so the tree
// never allocates per node and a deeply nested file cannot blow the C++ stack
// during destruction.
//
// Parsing is depth-limited and size-checked because project files can arrive
// from another player over the network.

#include <vector>
#include <string>

enum CPMJsonType
{
	CPM_JSON_NULL = 0,
	CPM_JSON_BOOL,
	CPM_JSON_NUMBER,
	CPM_JSON_STRING,
	CPM_JSON_ARRAY,
	CPM_JSON_OBJECT
};

class CPMJson
{
public:
	struct Node
	{
		int type;
		double num;
		bool boolean;
		std::string key;              // member name, empty outside an object
		std::string str;              // string value
		std::vector<int> children;    // indices into the node pool
	};

	std::vector<Node> nodes;
	int root;
	std::string error;

	CPMJson() : root(-1) {}

	// Parses a UTF-8 buffer. Returns false and fills `error` on malformed input.
	bool parse(const char *data, int len);

	bool valid() const { return root >= 0 && error.empty(); }

	// Lookups. Every accessor takes a node index and returns a default rather
	// than failing, so a missing optional field reads naturally.
	int member(int obj, const char *key) const;          // -1 if absent
	int at(int arr, int index) const;                    // -1 if out of range
	int count(int node) const;

	bool        getBool(int obj, const char *key, bool def) const;
	double      getNum(int obj, const char *key, double def) const;
	std::string getStr(int obj, const char *key, const char *def) const;

	// {"x":..,"y":..,"z":..} with per-component defaults.
	void getVec3(int obj, const char *key, float &x, float &y, float &z,
	             float dx, float dy, float dz) const;

	int type(int node) const { return (node >= 0 && node < (int)nodes.size()) ? nodes[node].type : CPM_JSON_NULL; }

private:
	const char *p;
	const char *end;
	int depth;

	void skipWs();
	int parseValue();
	int parseString();
	int parseNumber();
	int parseObject();
	int parseArray();
	int newNode(int type);
};
