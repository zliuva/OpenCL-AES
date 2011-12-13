#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sys/time.h>

#include <openssl/evp.h>
#include <openssl/engine.h>

#define MB			(1024 * 1024)
#define TOTAL_LEN	(1024UL * MB)

void run(const char *name, unsigned char *buf, int len) {
	int pass = TOTAL_LEN / len;
	
	unsigned char key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
	
	EVP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);
	
	int outlen = 0;
	struct timeval start, end;
	double total_time, throughput;
	
	EVP_CIPHER *cipher = (EVP_CIPHER *) EVP_get_cipherbyname("aes-256-ecb");
	
	EVP_CipherInit_ex(&ctx, cipher, NULL, key, NULL, 1);
	
	gettimeofday(&start, NULL);
	for (int i = 0; i < pass; i++) {
		fprintf(stderr, "%s: Pass %d/%d\n", name, i + 1, pass);
		// note we cannot run multiple pass on the same buffer, the CPU will cache it! (smart bastards at Intel)
		EVP_CipherUpdate(&ctx, buf + i * len, &outlen, buf, len);
		if (len != outlen) {
			fprintf(stderr, "Fatal error: incorrect output size.\n");
		}
	}
	
	gettimeofday(&end, NULL);
	total_time = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec) / 1000000.0;
	throughput = TOTAL_LEN * 8 / total_time / 1000000;
	printf("%s: %u-byte blocks, %lubytes in %f s, Throughput: %fMbps\n", name, len, TOTAL_LEN, total_time, throughput);
	
	EVP_CIPHER_CTX_cleanup(&ctx);
}

int main(int argc, char **argv) {
	OpenSSL_add_all_algorithms();
	
	int len = 128 * MB;
	if (argc > 1) {
		len = atoi(argv[1]) * MB;
	}
	
	unsigned char *buf = (unsigned char *) malloc(TOTAL_LEN);
	if (!buf) {
		fprintf(stderr, "Error Allocating Memory");
	}
	
	ENGINE_load_builtin_engines();
#ifdef OPENCL_ENGINE
	ENGINE *e = ENGINE_by_id("dynamic");
	if (!ENGINE_ctrl_cmd_string(e, "SO_PATH", OPENCL_ENGINE, 0) ||
		!ENGINE_ctrl_cmd_string(e, "LOAD", NULL, 0)) {
		fprintf(stderr, "Failed to load OpenCL engine!\n");
		return -1;
	}
	ENGINE_set_default(e, ENGINE_METHOD_ALL);
#endif
	
	run(argv[0], buf, len);
	
	free(buf);
	
	return 0;
}
