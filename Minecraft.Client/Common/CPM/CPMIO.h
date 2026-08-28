#pragma once
// CPM - Custom Player Models
//
// Port of com.tom.cpm.shared.io.IOHelper / ChecksumInputStream.
// Byte-exact reimplementation of the .cpmmodel wire encoding - do not "improve"
// any of the encodings here, they must match the Java mod bit for bit or models
// authored in the CPM editor will not load.
//
// All reads are bounds-checked. A short read sets the error flag and returns
// zero rather than throwing; callers check fail() once at the end of a block.
// Model data arrives over the network from untrusted peers, so this layer never
// trusts a length field.

#include <vector>
#include <string>

class CPMVec3
{
public:
	float x, y, z;

	CPMVec3() : x(0), y(0), z(0) {}
	CPMVec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

// Java's Short.MAX_VALUE / Vec3f.MAX_POS. Vec3f.MAX_POS is 256, so DIV is 127.
// Integer division in Java, so this is exactly 127, not 127.99609375.
#define CPM_FIXED_DIV 127

class CPMIn
{
private:
	const unsigned char *data;
	int len;
	int pos;
	bool failed;

	// Running checksum, enabled by beginChecksum(). Java's ChecksumInputStream
	// accumulates into a short and wraps on overflow.
	bool sumOn;
	short sum;

public:
	CPMIn(const unsigned char *data, int len);

	bool fail() const { return failed; }
	void setFail() { failed = true; }
	int remaining() const { return failed ? 0 : len - pos; }
	int position() const { return pos; }

	// Raw reads
	int read();                       // one unsigned byte, -1 at EOF
	signed char readByte();
	int readUnsignedByte();
	short readShort();
	int readUnsignedShort();
	int readInt();
	float readFloat();
	void readFully(unsigned char *out, int count);
	void skip(int count);

	// Java DataInput modified-UTF8 (2-byte length prefix)
	std::string readUTF();

	// CPM encodings
	int readVarInt();
	int readSignedVarInt();
	float readFloat2();               // short / 127
	float readVarFloat();             // signedVarInt / 127
	CPMVec3 readVec3ub();
	CPMVec3 readVec3b();
	CPMVec3 readVec6b();
	CPMVec3 readVarVec3();
	CPMVec3 readAngle();              // returns RADIANS

	// Length-prefixed sub-block. Returns false (and sets fail) on a bad length.
	bool readNextBlock(std::vector<unsigned char> &out);
	bool readByteArray(std::vector<unsigned char> &out);

	// Checksum support, for the outer .cpmmodel container
	void beginChecksum();
	bool checkSum();                  // reads the trailing 2 bytes and compares
};

class CPMOut
{
private:
	std::vector<unsigned char> buf;
	bool sumOn;
	short sum;

public:
	CPMOut();

	const std::vector<unsigned char> &bytes() const { return buf; }
	int size() const { return (int)buf.size(); }
	void clear() { buf.clear(); sum = 0; }

	void write(int b);
	void write(const unsigned char *d, int count);
	void writeBytes(const std::vector<unsigned char> &d);
	void writeByte(int v);
	void writeShort(int v);
	void writeInt(int v);
	void writeUTF(const std::string &s);

	void writeVarInt(int v);
	void writeSignedVarInt(int v);
	void writeFloat2(float f);
	void writeVarFloat(float f);
	void writeVarVec3(const CPMVec3 &v);
	// NOTE: asymmetric with readAngle, exactly as in the Java mod -
	// readAngle returns radians but writeAngle consumes DEGREES.
	void writeAngleDeg(const CPMVec3 &vDegrees);

	void writeByteArray(const std::vector<unsigned char> &d);
	void writeBlock(const std::vector<unsigned char> &d);   // varint length + data

	void beginChecksum();
	void writeChecksum();             // appends the 2 trailing sum bytes
};
