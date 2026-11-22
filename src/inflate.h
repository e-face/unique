#ifndef INFLATE_H
#define INFLATE_H

//----------------------------------------------

/**
 * @brief Inflate one zlib (RFC 1950) stream, as used by PDF FlateDecode.
 * @param out receives the uncompressed bytes. Cleared on failure.
 * @return 0 on success, a negative code on failure.
 */
int inflateZlib(const unsigned char *input, int inputLength,
                unsigned char *output, int outputCapacity, int *outputLength);

#endif // INFLATE_H
