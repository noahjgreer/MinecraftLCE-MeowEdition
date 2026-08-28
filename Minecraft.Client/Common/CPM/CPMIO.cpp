#include "stdafx.h"
#include "CPMIO.h"
#include <math.h>

// Matches ModelDefinitionLoader.MAX_BLOCK_SIZE / IOHelper.MAX_BLOCK_SIZE.
#define CPM_MAX_BLOCK_SIZE (16 * 1024 * 1024)

#ifndef CPM_PI
#define CPM_PI 3.14159265358979323846
#endif

//////////////////////////////////////////////////////////////////////////
// CPMIn
//////////////////////////////////////////////////////////////////////////

CPMIn::CPMIn(const unsigned char *data, int len) :
	data(data), len(len < 0 ? 0 : len), pos(0), failed(false), sumOn(false), sum(0)
{
	if (data == NULL) this->len = 0;
}

int CPMIn::read()
{
	if (failed || pos >= len) { failed = true; return -1; }
	int v = data[pos++];
	if (sumOn) sum = (short)(sum + v);
	return v;
}

signed char CPMIn::readByte()
{
	int v = read();
	return (signed char)(v < 0 ? 0 : v);
}

int CPMIn::readUnsignedByte()
{
	int v = read();
	return v < 0 ? 0 : v;
}

short CPMIn::readShort()
{
	int a = read();
	int b = read();
	if (failed) return 0;
	return (short)((a << 8) | b);
}

int CPMIn::readUnsignedShort()
{
	return (int)((unsigned short)readShort());
}

int CPMIn::readInt()
{
	int a = read(), b = read(), c = read(), d = read();
	if (failed) return 0;
	return (a << 24) | (b << 16) | (c << 8) | d;
}

float CPMIn::readFloat()
{
	int i = readInt();
	float f;
	memcpy(&f, &i, 4);
	return f;
}

void CPMIn::readFully(unsigned char *out, int count)
{
	if (count < 0) { failed = true; return; }
	for (int i = 0; i < count; i++)
	{
		int v = read();
		if (v < 0) { out[i] = 0; continue; }
		out[i] = (unsigned char)v;
	}
}

void CPMIn::skip(int count)
{
	for (int i = 0; i < count; i++) read();
}

std::string CPMIn::readUTF()
{
	int n = readUnsignedShort();
	if (failed || n < 0 || n > remaining()) { failed = true; return std::string(); }
	std::string s;
	s.resize(n);
	for (int i = 0; i < n; i++) s[i] = (char)read();
	// Java writes modified UTF-8. For the BMP, non-NUL range that CPM model and
	// author names actually use this is byte-identical to UTF-8, so the bytes
	// are passed through unchanged.
	return s;
}

int CPMIn::readVarInt()
{
	int i = 0;
	int j = 0;
	signed char b0;
	do
	{
		b0 = readByte();
		if (failed) return 0;
		i |= (b0 & 127) << (j++ * 7);
		if (j > 5) { failed = true; return 0; }
	}
	while ((b0 & 128) == 128);
	return i;
}

int CPMIn::readSignedVarInt()
{
	int i = 0;
	int sign = 1;
	signed char b0 = readByte();
	if (failed) return 0;
	if ((b0 & 0x40) != 0) sign = -1;
	i = b0 & 0x3F;
	int j = 6;
	while ((b0 & 0x80) != 0)
	{
		b0 = readByte();
		if (failed) return 0;
		i |= (b0 & 127) << j;
		j += 7;
		if (j > 34) { failed = true; return 0; }
	}
	return i * sign;
}

float CPMIn::readFloat2()
{
	return readShort() / (float)CPM_FIXED_DIV;
}

float CPMIn::readVarFloat()
{
	return readSignedVarInt() / (float)CPM_FIXED_DIV;
}

CPMVec3 CPMIn::readVec3ub()
{
	CPMVec3 v;
	v.x = readUnsignedByte() / 10.0f;
	v.y = readUnsignedByte() / 10.0f;
	v.z = readUnsignedByte() / 10.0f;
	return v;
}

CPMVec3 CPMIn::readVec3b()
{
	CPMVec3 v;
	v.x = readByte() / 10.0f;
	v.y = readByte() / 10.0f;
	v.z = readByte() / 10.0f;
	return v;
}

CPMVec3 CPMIn::readVec6b()
{
	CPMVec3 v;
	v.x = readFloat2();
	v.y = readFloat2();
	v.z = readFloat2();
	return v;
}

CPMVec3 CPMIn::readVarVec3()
{
	CPMVec3 v;
	v.x = readVarFloat();
	v.y = readVarFloat();
	v.z = readVarFloat();
	return v;
}

CPMVec3 CPMIn::readAngle()
{
	// Signed short, exactly as Java's DataInputStream.readShort. Angles above
	// 180 degrees were written as values > 32767 and come back negative here;
	// that wrap is part of the format and the renderer handles it fine.
	CPMVec3 v;
	v.x = (float)(readShort() / 65535.0 * 2 * CPM_PI);
	v.y = (float)(readShort() / 65535.0 * 2 * CPM_PI);
	v.z = (float)(readShort() / 65535.0 * 2 * CPM_PI);
	return v;
}

bool CPMIn::readNextBlock(std::vector<unsigned char> &out)
{
	out.clear();
	int size = readVarInt();
	if (failed) return false;
	if (size < 0 || size > CPM_MAX_BLOCK_SIZE || size > remaining())
	{
		failed = true;
		return false;
	}
	if (size == 0) return true;
	out.resize(size);
	readFully(&out[0], size);
	return !failed;
}

bool CPMIn::readByteArray(std::vector<unsigned char> &out)
{
	return readNextBlock(out);
}

void CPMIn::beginChecksum()
{
	sumOn = true;
	sum = 0;
}

bool CPMIn::checkSum()
{
	// The two trailing sum bytes are not themselves part of the sum.
	bool was = sumOn;
	sumOn = false;
	int ch1 = read();
	int ch2 = read();
	sumOn = was;
	if (ch1 < 0 || ch2 < 0) { failed = true; return false; }
	short expected = (short)((ch1 << 8) + ch2);
	if (expected != sum) { failed = true; return false; }
	return true;
}

//////////////////////////////////////////////////////////////////////////
// CPMOut
//////////////////////////////////////////////////////////////////////////

CPMOut::CPMOut() : sumOn(false), sum(0)
{
}

void CPMOut::write(int b)
{
	unsigned char v = (unsigned char)(b & 0xFF);
	buf.push_back(v);
	if (sumOn) sum = (short)(sum + (int)v);
}

void CPMOut::write(const unsigned char *d, int count)
{
	for (int i = 0; i < count; i++) write(d[i]);
}

void CPMOut::writeBytes(const std::vector<unsigned char> &d)
{
	if (!d.empty()) write(&d[0], (int)d.size());
}

void CPMOut::writeByte(int v) { write(v); }

void CPMOut::writeShort(int v)
{
	write((v >> 8) & 0xFF);
	write(v & 0xFF);
}

void CPMOut::writeInt(int v)
{
	write((v >> 24) & 0xFF);
	write((v >> 16) & 0xFF);
	write((v >> 8) & 0xFF);
	write(v & 0xFF);
}

void CPMOut::writeUTF(const std::string &s)
{
	writeShort((int)s.size());
	for (size_t i = 0; i < s.size(); i++) write((unsigned char)s[i]);
}

void CPMOut::writeVarInt(int v)
{
	unsigned int t = (unsigned int)v;
	while ((t & 0xFFFFFF80u) != 0)
	{
		write((int)((t & 127) | 128));
		t >>= 7;
	}
	write((int)t);
}

void CPMOut::writeSignedVarInt(int v)
{
	int sign = v < 0 ? 0x40 : 0;
	unsigned int t = (unsigned int)(v < 0 ? -v : v);
	int b = (int)((t & 0x3F) | sign);
	t >>= 6;
	while (t != 0)
	{
		write(b | 128);
		b = (int)(t & 127);
		t >>= 7;
	}
	write(b);
}

static int cpmClamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void CPMOut::writeFloat2(float f)
{
	writeShort(cpmClamp((int)(f * CPM_FIXED_DIV), -32768, 32767));
}

void CPMOut::writeVarFloat(float f)
{
	writeSignedVarInt((int)(f * CPM_FIXED_DIV));
}

void CPMOut::writeVarVec3(const CPMVec3 &v)
{
	writeVarFloat(v.x);
	writeVarFloat(v.y);
	writeVarFloat(v.z);
}

void CPMOut::writeAngleDeg(const CPMVec3 &v)
{
	writeShort(cpmClamp((int)(v.x / 360.0f * 65535), 0, 65535));
	writeShort(cpmClamp((int)(v.y / 360.0f * 65535), 0, 65535));
	writeShort(cpmClamp((int)(v.z / 360.0f * 65535), 0, 65535));
}

void CPMOut::writeByteArray(const std::vector<unsigned char> &d)
{
	writeVarInt((int)d.size());
	writeBytes(d);
}

void CPMOut::writeBlock(const std::vector<unsigned char> &d)
{
	writeVarInt((int)d.size());
	writeBytes(d);
}

void CPMOut::beginChecksum()
{
	sumOn = true;
	sum = 0;
}

void CPMOut::writeChecksum()
{
	short s = sum;
	sumOn = false;
	write((s >> 8) & 0xFF);
	write(s & 0xFF);
}
