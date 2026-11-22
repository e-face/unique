#include "inflate.h"
//----------------------------------------------
#include <string.h>

//----------------------------------------------

namespace
{

enum
{
    kMaxBits = 15,
    kMaxLits = 288,
    kMaxDists = 32,
    kMaxLengths = 19
};

struct BitIn
{
    const unsigned char *cursor;
    const unsigned char *end;
    unsigned long        buffer;
    int                  bits;
};

struct Huff
{
    int count[kMaxBits + 1];
    int symbol[kMaxLits];
};

//----------------------------------------------

int pullBits(BitIn *in, int need)
{
    unsigned long value = in->buffer;
    while (in->bits < need)
    {
        if (in->cursor >= in->end)
            return -1;
        value |= (unsigned long)(*in->cursor++) << in->bits;
        in->bits += 8;
    }
    in->buffer = value >> need;
    in->bits -= need;
    return (int)(value & ((1ul << need) - 1));
}

//----------------------------------------------

int buildHuff(Huff *huff, const int *lengths, int count)
{
    int len;
    int symbol;
    int left;
    int offs[kMaxBits + 1];

    memset(huff->count, 0, sizeof(huff->count));
    for (symbol = 0; symbol < count; ++symbol)
    {
        len = lengths[symbol];
        if (len < 0 || len > kMaxBits)
            return -1;
        huff->count[len] += 1;
    }
    if (huff->count[0] == count)
        return 0;

    left = 1;
    for (len = 1; len <= kMaxBits; ++len)
    {
        left <<= 1;
        left -= huff->count[len];
        if (left < 0)
            return -1;
    }

    offs[1] = 0;
    for (len = 1; len < kMaxBits; ++len)
        offs[len + 1] = offs[len] + huff->count[len];

    for (symbol = 0; symbol < count; ++symbol)
    {
        len = lengths[symbol];
        if (len != 0)
            huff->symbol[offs[len]++] = symbol;
    }
    return 0;
}

//----------------------------------------------

int decodeSymbol(BitIn *in, const Huff *huff)
{
    int len;
    int code = 0;
    int first = 0;
    int index = 0;
    for (len = 1; len <= kMaxBits; ++len)
    {
        int bit = pullBits(in, 1);
        int count;
        if (bit < 0)
            return -1;
        code |= bit;
        count = huff->count[len];
        if (code - count < first)
            return huff->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

//----------------------------------------------

const int kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

const int kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

const int kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};

const int kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

const int kCodeOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

//----------------------------------------------

int inflateBlocks(BitIn *in, unsigned char *out, int capacity, int *written)
{
    int lengths[kMaxLits + kMaxDists];
    int size = 0;
    int finalBlock = 0;

    while (!finalBlock)
    {
        int type;
        finalBlock = pullBits(in, 1);
        type = pullBits(in, 2);
        if (finalBlock < 0 || type < 0)
            return -1;

        if (type == 0)
        {
            int len;
            int nlen;
            in->buffer = 0;
            in->bits = 0;
            if (in->cursor + 4 > in->end)
                return -1;
            len = in->cursor[0] | (in->cursor[1] << 8);
            nlen = in->cursor[2] | (in->cursor[3] << 8);
            in->cursor += 4;
            if ((len ^ 0xffff) != nlen)
                return -1;
            if (in->cursor + len > in->end || size + len > capacity)
                return -1;
            memcpy(out + size, in->cursor, (size_t)len);
            in->cursor += len;
            size += len;
            continue;
        }

        Huff lit;
        Huff dist;
        if (type == 1)
        {
            int fixed[kMaxLits];
            int i;
            int distLen[kMaxDists];
            for (i = 0; i < 144; ++i)
                fixed[i] = 8;
            for (; i < 256; ++i)
                fixed[i] = 9;
            for (; i < 280; ++i)
                fixed[i] = 7;
            for (; i < 288; ++i)
                fixed[i] = 8;
            if (buildHuff(&lit, fixed, 288) != 0)
                return -1;
            for (i = 0; i < 32; ++i)
                distLen[i] = 5;
            if (buildHuff(&dist, distLen, 32) != 0)
                return -1;
        }
        else if (type == 2)
        {
            int nlit = pullBits(in, 5);
            int ndist = pullBits(in, 5);
            int nlen = pullBits(in, 4);
            int codeLens[19];
            Huff codeHuff;
            int i;
            int count;
            if (nlit < 0 || ndist < 0 || nlen < 0)
                return -1;
            nlit += 257;
            ndist += 1;
            nlen += 4;
            if (nlit > 286 || ndist > 30)
                return -1;
            memset(codeLens, 0, sizeof(codeLens));
            for (i = 0; i < nlen; ++i)
            {
                int v = pullBits(in, 3);
                if (v < 0)
                    return -1;
                codeLens[kCodeOrder[i]] = v;
            }
            if (buildHuff(&codeHuff, codeLens, 19) != 0)
                return -1;
            count = nlit + ndist;
            i = 0;
            while (i < count)
            {
                int symbol = decodeSymbol(in, &codeHuff);
                int repeat;
                int value;
                if (symbol < 0)
                    return -1;
                if (symbol < 16)
                {
                    lengths[i++] = symbol;
                    continue;
                }
                if (symbol == 16)
                {
                    int extra = pullBits(in, 2);
                    if (extra < 0 || i == 0)
                        return -1;
                    value = lengths[i - 1];
                    repeat = 3 + extra;
                }
                else if (symbol == 17)
                {
                    int extra = pullBits(in, 3);
                    if (extra < 0)
                        return -1;
                    value = 0;
                    repeat = 3 + extra;
                }
                else
                {
                    int extra = pullBits(in, 7);
                    if (extra < 0)
                        return -1;
                    value = 0;
                    repeat = 11 + extra;
                }
                if (i + repeat > count)
                    return -1;
                while (repeat-- > 0)
                    lengths[i++] = value;
            }
            if (buildHuff(&lit, lengths, nlit) != 0)
                return -1;
            if (buildHuff(&dist, lengths + nlit, ndist) != 0)
                return -1;
        }
        else
        {
            return -1;
        }

        for (;;)
        {
            int symbol = decodeSymbol(in, &lit);
            int length;
            int distance;
            int extra;
            int from;
            int i;
            if (symbol < 0)
                return -1;
            if (symbol < 256)
            {
                if (size >= capacity)
                    return -1;
                out[size++] = (unsigned char)symbol;
                continue;
            }
            if (symbol == 256)
                break;
            if (symbol > 285)
                return -1;
            extra = pullBits(in, kLenExtra[symbol - 257]);
            if (extra < 0)
                return -1;
            length = kLenBase[symbol - 257] + extra;
            symbol = decodeSymbol(in, &dist);
            if (symbol < 0 || symbol > 29)
                return -1;
            extra = pullBits(in, kDistExtra[symbol]);
            if (extra < 0)
                return -1;
            distance = kDistBase[symbol] + extra;
            if (distance <= 0 || distance > size || size + length > capacity)
                return -1;
            from = size - distance;
            for (i = 0; i < length; ++i)
                out[size++] = out[from + i];
        }
    }

    *written = size;
    return 0;
}

//----------------------------------------------

unsigned long adler32Of(const unsigned char *data, int length)
{
    unsigned long s1 = 1;
    unsigned long s2 = 0;
    int i;
    for (i = 0; i < length; ++i)
    {
        s1 = (s1 + data[i]) % 65521ul;
        s2 = (s2 + s1) % 65521ul;
    }
    return (s2 << 16) | s1;
}

} // namespace

//----------------------------------------------

int inflateZlib(const unsigned char *input, int inputLength,
                unsigned char *output, int outputCapacity, int *outputLength)
{
    BitIn in;
    int cmf;
    int flg;
    int written = 0;
    unsigned long have;
    unsigned long expect;
    if (!input || inputLength < 6 || !output || outputCapacity <= 0 || !outputLength)
        return -1;

    cmf = input[0];
    flg = input[1];
    if ((cmf & 0x0f) != 8)
        return -1;
    if ((cmf * 256 + flg) % 31 != 0)
        return -1;
    if (flg & 0x20)
        return -1;

    in.cursor = input + 2;
    in.end = input + inputLength;
    in.buffer = 0;
    in.bits = 0;
    if (inflateBlocks(&in, output, outputCapacity, &written) != 0)
        return -1;

    // Adler32 is byte-aligned after the deflate blocks.
    in.buffer = 0;
    in.bits = 0;
    if (in.cursor + 4 > in.end)
        return -1;
    have = adler32Of(output, written);
    expect = ((unsigned long)in.cursor[0] << 24)
           | ((unsigned long)in.cursor[1] << 16)
           | ((unsigned long)in.cursor[2] << 8)
           | (unsigned long)in.cursor[3];
    if (have != expect)
        return -1;
    *outputLength = written;
    return 0;
}
