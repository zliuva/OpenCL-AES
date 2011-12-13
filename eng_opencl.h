/**
 * Copyright 2011 University of Virginia. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ''AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * OpenCL Support for OpenSSL
 *
 * Zhengyang Liu <zl4ef@virginia.edu>, Ashwin Raghav Mohan Ganesh <am2qa@virginia.edu>
 *
 */

#ifndef OpenSSL_eng_opencl_h
#define OpenSSL_eng_opencl_h

// helper macros

#define BLOCK_CIPHER_generic(nid,keylen,blocksize,ivlen,nmode,mode,MODE,flags) \
static const EVP_CIPHER opencl_aes_##keylen##_##mode = { \
nid##_##keylen##_##nmode,blocksize,	\
keylen/8,ivlen, \
flags|EVP_CIPH_##MODE##_MODE,	\
opencl_aes_init_key,			\
opencl_aes_##mode##_cipher,		\
NULL,				\
sizeof(OPENCL_AES_KEY),		\
NULL,NULL,NULL,NULL }; \
const EVP_CIPHER *EVP_opencl_##keylen##_##mode(void) \
{ return &opencl_aes_##keylen##_##mode; }

#define BLOCK_CIPHER_generic_pack(nid,keylen,flags)		\
BLOCK_CIPHER_generic(nid,keylen,16,0,ecb,ecb,ECB,flags|EVP_CIPH_FLAG_DEFAULT_ASN1)

#endif
