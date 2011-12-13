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

#include <openssl/engine.h>
#include <openssl/aes.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <string.h>
#include <assert.h>

#ifdef __APPLE__
	#include <OpenCL/OpenCL.h>
#else
	#include <CL/opencl.h>
#endif

#include "eng_opencl.h"

#define DYNAMIC_ENGINE

//#define OPENCLDIR			OPENSSLDIR"/opencl"
#define AES_KERNEL			"eng_opencl_aes.cl"
#define MAX_BUFFER_SIZE		128 * 1024 * 1024

void opencl_error(const char *errinfo, const void *private_info, size_t cb, void *user_data) {
	fprintf(stderr, "OpenCL Error: %s\n", errinfo);
}


// shit happens here !!!

static const char *load_kernel_source(const char *filename) {
	size_t len = strlen(OPENCLDIR) + 1 + strlen(filename) + 1;
	char *fullpath = (char *) malloc(len);
	sprintf(fullpath, "%s/%s", OPENCLDIR, filename);
	fullpath[len] = '\0';
	
	int fd = open(fullpath, O_RDONLY);
	
	struct stat st;
	fstat(fd, &st);
	
	char *buf = (char *) malloc(st.st_size + 1);
	assert(st.st_size == read(fd, buf, st.st_size));
	
	buf[st.st_size] = '\0';
	
	return buf;
}

typedef struct {
	AES_KEY ks;
	cl_kernel kernel;
} OPENCL_AES_KEY;

static cl_context context;
static cl_command_queue queue;
static cl_program program;
static cl_kernel encrypt_kernel;
static cl_kernel decrypt_kernel;
static size_t local_work_size = 128;
static cl_mem buffer_state;
static cl_mem buffer_roundkeys;

static int opencl_init(ENGINE *e) {
	cl_int err = 0;
	cl_uint numPlatforms;
	
	err = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (err != CL_SUCCESS || !numPlatforms) {
		fprintf(stderr, "Failed to get OpenCL platforms\n");
		return 0;
	}
	
	cl_platform_id *platforms = (cl_platform_id *) malloc(sizeof(cl_platform_id) * numPlatforms);
	
	err = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Failed to get OpenCL platforms\n");
		return 0;
	}
	
	cl_platform_id platform = platforms[0];
	
	/* print platform information */
	
	cl_device_id device = NULL;
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "No GPU support, falling back to CPU\n");
		err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, NULL);
	}
	if (!device) {
		fprintf(stderr, "Error getting device\n");
		return 0;
	}
	
	context = clCreateContext(0, 1, &device, opencl_error, NULL, &err);
	if (!context) {
		fprintf(stderr, "Error creating context\n");
		return 0;
	}
	
	queue = clCreateCommandQueue(context, device, 0, &err);
	if (!queue) {
		fprintf(stderr, "Error creating command queue\n");
		return 0;
	}
	
	const char *kernel_source = load_kernel_source(AES_KERNEL);
	program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, &err);
	if (!program || err != CL_SUCCESS) {
		fprintf(stderr, "Error creating program\n");
		return 0;
	}
	
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];
		
		fprintf(stderr, "Error building program\n");
		clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		fprintf(stderr, "%s\n", buffer);
		return 0;
	}
	
	encrypt_kernel = clCreateKernel(program, "AES_encrypt", &err);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error creating encrypt kernel\n");
		return 0;
	}
	
	if (getenv("OPENSSL_OPENCL_LOCAL_WORK_SIZE")) {
		local_work_size = atoi(getenv("OPENSSL_OPENCL_LOCAL_WORK_SIZE"));
	}
	
	if (getenv("OPENSSL_OPENCL_USE_LOCAL_T_TABLE")) {
		local_work_size = 256; // must be this value to work
		encrypt_kernel = clCreateKernel(program, "AES_encrypt_local", &err);
		if (err != CL_SUCCESS) {
			fprintf(stderr, "Error creating encrypt kernel\n");
			return 0;
		}
	}

	int max_buffer_size = MAX_BUFFER_SIZE;

	if (getenv("OPENSSL_OPENCL_MAX_BUFFER_SIZE")) {
		max_buffer_size = atoi(getenv("OPENSSL_OPENCL_MAX_BUFFER_SIZE")) * 1024 * 1024;
	}
	
	// dynamic buffer size please
	buffer_state = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, max_buffer_size, NULL, &err);
	buffer_roundkeys = clCreateBuffer(context, CL_MEM_READ_ONLY, 16 * 15, NULL, &err);
	
	err  = clSetKernelArg(encrypt_kernel, 0, sizeof(cl_mem), &buffer_state);
	err |= clSetKernelArg(encrypt_kernel, 1, sizeof(cl_mem), &buffer_roundkeys);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error setting arguments\n");
		return 0;
	}
	
	return 1;
}

static int opencl_finish(ENGINE *e) {
	clReleaseMemObject(buffer_state);
	clReleaseMemObject(buffer_roundkeys);
	clReleaseContext(context);
	clReleaseKernel(encrypt_kernel);
	clReleaseKernel(decrypt_kernel);
	clReleaseProgram(program);
	clReleaseCommandQueue(queue);
	
	return 1;
}

static int opencl_aes_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, size_t len) {
	OPENCL_AES_KEY *dat = (OPENCL_AES_KEY *)ctx->cipher_data;
	
	cl_kernel kernel = dat->kernel;
	
	cl_int err = 0;
	
	size_t local[1] = {local_work_size};
	size_t global[1] = {(len / 16 + local[0] - 1) / local[0] * local[0]};
	
	//fprintf(stderr, "Global Work Size: %d\n", global[0]);
	//fprintf(stderr, "Len: %d\n", len);
	
	err = clSetKernelArg(kernel, 2, sizeof(dat->ks.rounds), &dat->ks.rounds);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error setting arguments\n");
		return 0;
	}
	
	clEnqueueWriteBuffer(queue, buffer_state, CL_FALSE, 0, len, in, 0, NULL, NULL);
	clEnqueueWriteBuffer(queue, buffer_roundkeys, CL_FALSE, 0, 16 * 15, &dat->ks.rd_key, 0, NULL, NULL);
	
	err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, global, local, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error executing kernel: %d\n", err);
		return 0;
	}
	
	err = clEnqueueReadBuffer(queue, buffer_state, CL_FALSE, 0, len, out, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error reading results: %d\n", err);
		return 0;
	}
	
	err = clFinish(queue);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error finishing queue: %d\n", err);
		return 0;
	}
	
	return 1;
}

// delcare the ciphers

static int opencl_aes_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key, const unsigned char *iv, int enc) {
	int ret, mode;
	OPENCL_AES_KEY *dat = (OPENCL_AES_KEY *)ctx->cipher_data;
	
	// note the key here MUST be BIG endian when represented as integer!!!!
	mode = ctx->cipher->flags & EVP_CIPH_MODE;
	if ((mode == EVP_CIPH_ECB_MODE || mode == EVP_CIPH_CBC_MODE) && !enc) {
		ret = AES_set_decrypt_key(key,ctx->key_len*8,&dat->ks);
		dat->kernel = decrypt_kernel;
	} else {
		ret = AES_set_encrypt_key(key, ctx->key_len * 8, &dat->ks);
		dat->kernel = encrypt_kernel;
	}
	
	if(ret < 0) {
		EVPerr(EVP_F_AES_INIT_KEY,EVP_R_AES_KEY_SETUP_FAILED);
		return 0;
	}
	
	return 1;
}

BLOCK_CIPHER_generic_pack(NID_aes,128,EVP_CIPH_FLAG_FIPS)
BLOCK_CIPHER_generic_pack(NID_aes,192,EVP_CIPH_FLAG_FIPS)
BLOCK_CIPHER_generic_pack(NID_aes,256,EVP_CIPH_FLAG_FIPS)

static int opencl_cipher_nids[] = {
	NID_aes_128_ecb,
	NID_aes_192_ecb,
	NID_aes_256_ecb// more to come !!!
};
static int opencl_cipher_nids_number = 3;

static int opencl_ciphers(ENGINE *e, const EVP_CIPHER **cipher, const int **nids, int nid) {
	if(!cipher) {
		/* We are returning a list of supported nids */
		*nids = opencl_cipher_nids;
		return opencl_cipher_nids_number;
	}
	
	/* We are being asked for a specific cipher */
	if(nid == NID_aes_128_ecb) {
		*cipher = &opencl_aes_128_ecb;
	} else if(nid == NID_aes_192_ecb) {
		*cipher = &opencl_aes_192_ecb;
	} else if(nid == NID_aes_256_ecb) {
		*cipher = &opencl_aes_256_ecb;
	} else {
		*cipher = NULL;
		return 0;
	}
	
	return 1;
}

// Register the engine

/* The constants used when creating the ENGINE */
static const char *engine_opencl_id = "OpenCL";
static const char *engine_opencl_name = "OpenCL support";

/* This internal function is used by ENGINE_opencl() and possibly by the
 * "dynamic" ENGINE support too */
static int bind_helper(ENGINE *e) {
	if(!ENGINE_set_id(e, engine_opencl_id)
	   || !ENGINE_set_name(e, engine_opencl_name)
	   || !ENGINE_set_ciphers(e, opencl_ciphers)
	   || !ENGINE_set_init_function(e, opencl_init)
	   || !ENGINE_set_finish_function(e, opencl_finish)) {
		return 0;
	}
	
	/* openssl_load_error_strings(); */
	
	return 1;
}


static ENGINE *engine_opencl(void) {
	ENGINE *ret = ENGINE_new();
	
	if(!ret) {
		return NULL;
	}
	
	if(!bind_helper(ret)) {
		ENGINE_free(ret);
		return NULL;
	}
	
	return ret;
}

void ENGINE_load_opencl(void) {
	ENGINE *toadd = engine_opencl();
	if(!toadd) return;
	ENGINE_add(toadd);
	/* If the "add" worked, it gets a structural reference. So either way,
	 * we release our just-created reference. */
	ENGINE_free(toadd);
	ERR_clear_error();
}

static int bind_engine_opencl(ENGINE *e, const char *id) {
	if (!bind_helper(e))  {
		fprintf(stderr, "bind failed\n");
		return 0;
	}
	
	return 1;
}

IMPLEMENT_DYNAMIC_CHECK_FN()
IMPLEMENT_DYNAMIC_BIND_FN(bind_engine_opencl)
