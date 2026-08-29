#pragma once

#include "Tag.h"
#include "System.h"

// 4J Meow - TAG_Long_Array (12). Absent from the LCE tree because the McRegion-era
// format never used it; the Anvil section palettes store their bit-packed block
// indices as a long[], so it is required for Java-compatible saves.
class LongArrayTag : public Tag
{
public:
	longArray data;

	LongArrayTag(const wstring &name) : Tag(name)
	{
	}

	LongArrayTag(const wstring &name, longArray data) : Tag(name)
	{
		this->data = data;
	}

	void write(DataOutput *dos)
	{
		dos->writeInt(data.length);
		for (unsigned int i = 0; i < data.length; i++)
		{
			dos->writeLong(data[i]);
		}
	}

	void load(DataInput *dis)
	{
		int length = dis->readInt();

		if ( data.data ) delete[] data.data;
		data = longArray(length);
		for (int i = 0; i < length; i++)
		{
			data[i] = dis->readLong();
		}
	}

	byte getId() { return TAG_Long_Array; }

	wstring toString()
	{
		static wchar_t buf[32];
		swprintf(buf, 32, L"[%d longs]",data.length);
		return wstring( buf );
	}

	bool equals(Tag *obj)
	{
		if (Tag::equals(obj))
		{
			LongArrayTag *o = (LongArrayTag *) obj;
			return ((data.data == NULL && o->data.data == NULL) || (data.data != NULL && data.length == o->data.length && memcmp(data.data, o->data.data, data.length * sizeof(__int64)) == 0) );
		}
		return false;
	}

	Tag *copy()
	{
		longArray cp = longArray(data.length);
		System::arraycopy(data, 0, &cp, 0, data.length);
		return new LongArrayTag(getName(), cp);
	}
};
