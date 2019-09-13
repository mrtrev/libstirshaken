#include "stir_shaken.h"


static X509_NAME *parse_name(const char *cp, long chtype, int canmulti)
{
	int nextismulti = 0;
	char *work;
	X509_NAME *n;

	if (*cp++ != '/') {
		return NULL;
	}

	n = X509_NAME_new();
	if (n == NULL)
		return NULL;
	work = OPENSSL_strdup(cp);
	if (work == NULL) {
		goto err;
	}

	while (*cp) {
		char *bp = work;
		char *typestr = bp;
		unsigned char *valstr;
		int nid;
		int ismulti = nextismulti;
		nextismulti = 0;

		while (*cp && *cp != '=')
			*bp++ = *cp++;
		if (*cp == '\0') {
			goto err;
		}
		*bp++ = '\0';
		++cp;

		valstr = (unsigned char *)bp;
		for (; *cp && *cp != '/'; *bp++ = *cp++) {
			if (canmulti && *cp == '+') {
				nextismulti = 1;
				break;
			}
			if (*cp == '\\' && *++cp == '\0') {
				goto err;
			}
		}
		*bp++ = '\0';

		if (*cp)
			++cp;

		nid = OBJ_txt2nid(typestr);
		if (nid == NID_undef) {
			continue;
		}
		if (*valstr == '\0') {
			continue;
		}
		if (!X509_NAME_add_entry_by_NID(n, nid, chtype,valstr, strlen((char *)valstr), -1, ismulti ? -1 : 0)) {
			goto err;
		}
	}

	OPENSSL_free(work);
	return n;

err:
	X509_NAME_free(n);
	OPENSSL_free(work);
	return NULL;
}

/*
 * subject is expected to be in the format /type0=value0/type1=value1/type2=...
 * where characters may be escaped by \
 */
static int build_subject(X509_REQ *req, const char *subject, unsigned long chtype,
		int multirdn)
{
	X509_NAME *n;

	if ((n = parse_name(subject, chtype, multirdn)) == NULL)
		return 0;

	if (!X509_REQ_set_subject_name(req, n)) {
		X509_NAME_free(n);
		return 0;
	}
	X509_NAME_free(n);
	return 1;
}

static int do_sign_init(EVP_MD_CTX *ctx, EVP_PKEY *pkey,
		const EVP_MD *md, STACK_OF(OPENSSL_STRING) *sigopts)
{
	EVP_PKEY_CTX    *pkctx = NULL;
	int             def_nid = 0;

	if (ctx == NULL)
		return 0;
	/*
	 * EVP_PKEY_get_default_digest_nid() returns 2 if the digest is mandatory
	 * for this algorithm.
	 */
	if (EVP_PKEY_get_default_digest_nid(pkey, &def_nid) == 2
			&& def_nid == NID_undef) {
		/* The signing algorithm requires there to be no digest */
		md = NULL;
	}
	if (!EVP_DigestSignInit(ctx, &pkctx, md, NULL, pkey))
		return 0;

	/** Not for now
	  for (i = 0; i < sk_OPENSSL_STRING_num(sigopts); i++) {
	  char *sigopt = sk_OPENSSL_STRING_value(sigopts, i);
	  if (pkey_ctrl_string(pkctx, sigopt) <= 0) {
	  BIO_printf(bio_err, "parameter error \"%s\"\n", sigopt);
	  ERR_print_errors(bio_err);
	  return 0;
	  }
	  }**/
	return 1;
}

int do_X509_REQ_sign(X509_REQ *x, EVP_PKEY *pkey, const EVP_MD *md,
		STACK_OF(OPENSSL_STRING) *sigopts)
{
	int rv;

#if OPENSSL_VERSION_MINOR < 1
	EVP_MD_CTX *mctx = EVP_MD_CTX_create();
#else
	EVP_MD_CTX *mctx = EVP_MD_CTX_new();
#endif
	EVP_MD_CTX_init(mctx);
	rv = do_sign_init(mctx, pkey, md, sigopts);
	if (rv > 0)
		rv = X509_REQ_sign_ctx(x, mctx);
#if OPENSSL_VERSION_MINOR < 1
	EVP_MD_CTX_destroy(mctx);
#else
	EVP_MD_CTX_free(mctx);
#endif
	return rv > 0 ? 1 : 0;
}

static unsigned char *generic_asn1(const char *value, X509V3_CTX *ctx,
		long *ext_len)
{
	ASN1_TYPE *typ;
	unsigned char *ext_der = NULL;
	typ = ASN1_generate_v3((char*)value, ctx);
	if (typ == NULL)
		return NULL;
	*ext_len = i2d_ASN1_TYPE(typ, &ext_der);
	ASN1_TYPE_free(typ);
	return ext_der;
}
/**
  static void delete_ext(STACK_OF(X509_EXTENSION) *sk, X509_EXTENSION *dext)
  {
  int idx;
  ASN1_OBJECT *obj;
  obj = X509_EXTENSION_get_object(dext);
  while ((idx = X509v3_get_ext_by_OBJ(sk, obj, -1)) >= 0) {
  X509_EXTENSION *tmpext = X509v3_get_ext(sk, idx);
  X509v3_delete_ext(sk, idx);
  X509_EXTENSION_free(tmpext);
  }
  }
 **/
int OPENSSL_hexchar2int(unsigned char c)
{
#ifdef CHARSET_EBCDIC
	c = os_toebcdic[c];
#endif

	switch (c) {
		case '0':
			return 0;
		case '1':
			return 1;
		case '2':
			return 2;
		case '3':
			return 3;
		case '4':
			return 4;
		case '5':
			return 5;
		case '6':
			return 6;
		case '7':
			return 7;
		case '8':
			return 8;
		case '9':
			return 9;
		case 'a': case 'A':
			return 0x0A;
		case 'b': case 'B':
			return 0x0B;
		case 'c': case 'C':
			return 0x0C;
		case 'd': case 'D':
			return 0x0D;
		case 'e': case 'E':
			return 0x0E;
		case 'f': case 'F':
			return 0x0F;
	}
	return -1;
}

/*
 *  * Give a string of hex digits convert to a buffer
 *   */
unsigned char *OPENSSL_hexstr2buf(const char *str, long *len)
{
	unsigned char *hexbuf, *q; 
	unsigned char ch, cl; 
	int chi, cli;
	const unsigned char *p; 
	size_t s;

	s = strlen(str);
	if ((hexbuf = OPENSSL_malloc(s >> 1)) == NULL) {
		return NULL;
	}   
	for (p = (const unsigned char *)str, q = hexbuf; *p; ) { 
		ch = *p++;
		if (ch == ':')
			continue;
		cl = *p++;
		if (!cl) {
			OPENSSL_free(hexbuf);
			return NULL;
		}
		cli = OPENSSL_hexchar2int(cl);
		chi = OPENSSL_hexchar2int(ch);
		if (cli < 0 || chi < 0) {
			OPENSSL_free(hexbuf);
			return NULL;
		}
		*q++ = (unsigned char)((chi << 4) | cli);
	}   

	if (len)
		*len = q - hexbuf;
	return hexbuf;
}


/* Create a generic extension: for now just handle DER type */
static X509_EXTENSION *v3_generic_extension(const char *ext, const char *value, int crit, int gen_type, X509V3_CTX *ctx)
{
	unsigned char *ext_der = NULL;
	long ext_len = 0;
	ASN1_OBJECT *obj = NULL;
	ASN1_OCTET_STRING *oct = NULL;
	X509_EXTENSION *extension = NULL;

	if ((obj = OBJ_txt2obj(ext, 0)) == NULL) {
		goto err;
	}

	if (gen_type == 1)
		ext_der = OPENSSL_hexstr2buf(value, &ext_len);
	else if (gen_type == 2)
		ext_der = generic_asn1(value, ctx, &ext_len);

	if (ext_der == NULL) {
		goto err;
	}

	if ((oct = ASN1_OCTET_STRING_new()) == NULL) {
		goto err;
	}

	oct->data = ext_der;
	oct->length = ext_len;
	ext_der = NULL;

	extension = X509_EXTENSION_create_by_OBJ(NULL, obj, crit, oct);

err:
	ASN1_OBJECT_free(obj);
	ASN1_OCTET_STRING_free(oct);
	OPENSSL_free(ext_der);
	return extension;

}

static int stir_shaken_v3_add_extensions(X509V3_CTX *ctx, const char *ext_name, const char *ext_value, X509_REQ *req, X509 *x)
{
	int                         i = 0, ext_type = -1, crit = 0;
	X509_EXTENSION              *ext = NULL;
	STACK_OF(X509_EXTENSION)    *extlist = NULL, **sk = NULL;
	uint8_t                     doing_req = 0, doing_x = 0;

	if (req) {
		sk = &extlist;
		doing_req = 1;
	}

	if (x) {
		doing_x = 1;
	}

	if ((doing_req ^ doing_x) == 0) return 0;

	if (doing_x) {

		// Handle adding of extensions for certificate generation
		crit = 0;   // TODO v3_check_critical(&ext_value);
		ext_type = 1;  // TODO this may fail at some future, should they change type definitions, can 
		if (!(ext = v3_generic_extension(ext_name, ext_value, crit, ext_type, NULL)))
			return 0;

		if (!ext) return 0;
		if (X509_add_ext(x, ext, -1) == 0) {
			X509_EXTENSION_free(ext);
			return 0;
		}

		X509_EXTENSION_free(ext);
		return 1;
	}

	if ((ext = X509V3_EXT_nconf(NULL, ctx, (char *)ext_name, (char*)ext_value)) == NULL)
		return 0;
	//if (ctx->flags == X509V3_CTX_REPLACE)
	//    delete_ext(*sk, ext);
	if (sk != NULL) {
		if (X509v3_add_ext(sk, ext, -1) == NULL) {
			X509_EXTENSION_free(ext);
			return 0;
		}
	}
	X509_EXTENSION_free(ext);
	i = X509_REQ_add_extensions(req, extlist);
	sk_X509_EXTENSION_pop_free(extlist, X509_EXTENSION_free);

	return i;
}

static int stir_shaken_generate_der(char *der, int der_len, uint32_t sp_code, int include_der_string)
{
	int             len = -1, tpl_len = -1, i = 0;
	// config is in form "SEQUENCE:tn_auth_list\n[tn_auth_list]\nfield1=EXP:0,IA5:1237";
	const char      *der_string = "DER:";
	const char      *der_template = "30:08:a0:06:16:04";   // TNAuthorizationList extension value, this is "field1=EXP:0,IA5:" in DER format
	char sp_code_str[100] = {0};

	len = snprintf(sp_code_str, 100, "%u", sp_code);
	if (len >= 100) {
		return -1;
	}

	tpl_len = snprintf(der, der_len, "%s%s", include_der_string ? der_string : "", der_template);
	if (tpl_len >= der_len) {
		return -1;
	}

	if (3*len + tpl_len + 1 > der_len) {
		return -1;
	}

	i = 0;
	len = strlen(sp_code_str);
	while (len) {
		der[tpl_len + 3*i] = ':';
		der[tpl_len + 3*i + 1] = '3';
		der[tpl_len + 3*i + 2] = sp_code_str[i];
		++i;
		len--;
	}
	der[tpl_len + 3*i] = '\0';

	return tpl_len + 3*i; // excluding '\0'
}

#define EXT_VALUE_DER_LEN 300
stir_shaken_status_t stir_shaken_generate_csr(uint32_t sp_code, X509_REQ **csr_req, EVP_PKEY *private_key, EVP_PKEY *public_key, const char *csr_full_name, const char *csr_text_full_name)
{
	BIO                     *out = NULL, *bio = NULL;
	X509_REQ                *req = NULL;
	const char              *req_subj = "/C=US/ST=VA/L=ItsHere/O=SignalWire, Inc./OU=VOIP/CN=SHAKEN";
	int                     req_multirdn = 0;
	unsigned long           req_chtype = 4097;
	X509V3_CTX              ext_ctx = {0};
	const EVP_MD            *digest = NULL;
	// our lovely extension configuration (input to DER) is:
	// asn1=SEQUENCE:tn_auth_list
	// [tn_auth_list]
	// field1=EXP:0,IA5:1237
	//const char *der_input = "SEQUENCE:tn_auth_list\n[tn_auth_list]\nfield1=EXP:0,IA5:1237";
	const char              *ext_name = "1.3.6.1.5.5.7.1.26";                   // our lovely TNAuthorizationList extension identifier that we will use to construct v3 extension of type TNAuthorizationList
	//const char              *ext_value = "DER:30:08:a0:06:16:04:37:38:36:35";   // TNAuthorizationList extension value, this is "asn1=SEQUENCE:tn_auth_list\ntn_auth_list]\nfield1=EXP:0,IA5:1237" in DER format
	char                    ext_value_der[EXT_VALUE_DER_LEN] = {0};
	int                     der_len = -1, include_der_string = 1;
	size_t                  i = 0;

	if (!sp_code || !csr_req) return STIR_SHAKEN_STATUS_FALSE;
	out = BIO_new(BIO_s_file());
	if (!out) return STIR_SHAKEN_STATUS_FALSE;

	req = X509_REQ_new();
	if (!req) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Cannot create CSR\n");
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: New CSR request created\n");

	// Make request (similar to OpenSSL's make_REQ from req.c

	/* setup version number */
	if (!X509_REQ_set_version(req, 0L)) {
		goto fail;               /* version 1 */
	}

	i = build_subject(req, req_subj, req_chtype, req_multirdn);
	if (i == 0) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Unable to build CSR's subject\n");
		goto fail;
	}

	if (!X509_REQ_set_pubkey(req, public_key)) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Cannot set EVP_KEY to CSR\n");
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: Prepared CSR request for signing\n");

	// Set up V3 context struct
	X509V3_set_ctx(&ext_ctx, NULL, NULL, req, NULL, 0);

	// DER
	include_der_string = 1;
	der_len = stir_shaken_generate_der(ext_value_der, EXT_VALUE_DER_LEN, sp_code, include_der_string);
	if (der_len == -1) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Failed to generate DER\n");
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: Got DER (len=%d): %s\n", der_len, ext_value_der);


	// Add extensions
	i = stir_shaken_v3_add_extensions(&ext_ctx, ext_name, ext_value_der, req, NULL);
	if (i == 0) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Cannot load extensions into CSR\n");
		goto fail;
	}

	digest = EVP_get_digestbyname("sha256");
	if (!digest) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Failed loading digest\n");
		goto fail;
	}

	i = do_X509_REQ_sign(req, private_key, digest, NULL);
	if (i == 0) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Failed to sign CSR\n");
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: Signed CSR\n");

	if (csr_full_name) {
		i = BIO_write_filename(out, (char*)csr_full_name);
		if (i == 0) {
			// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Failed to redirect bio to file %s\n", csr_full_name);
			goto fail;
		}

		i = PEM_write_bio_X509_REQ(out, req);
		if (i == 0) {
			// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR ERR: Failed to write CSR to file %s\n", csr_full_name);
			goto fail;
		}
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: Written CSR to file %s\n", csr_full_name);
	}

	BIO_free_all(out);

	if (csr_text_full_name) {
		bio = BIO_new_file(csr_text_full_name, "w");
		if (!bio) goto anyway;
		X509_REQ_print_ex(bio, req, 0, 0);
		BIO_free_all(bio);
		// TODO set error string, allow for retrieval printf("STIR-Shaken: CSR: Written CSR in human readable form to file %s\n", csr_text_full_name);
	}

anyway:

	*csr_req = req;

	return STIR_SHAKEN_STATUS_OK;

fail:
	if (out) BIO_free_all(out);
	//if (pkey) EVP_PKEY_free(pkey);
	if (req) X509_REQ_free(req);
	return STIR_SHAKEN_STATUS_FALSE;
}

X509 * stir_shaken_generate_x509_self_sign(uint32_t sp_code, X509_REQ *req, EVP_PKEY *private_key)
{
	X509            *x = NULL;
	EVP_PKEY        *pkey = NULL;
	ASN1_INTEGER    *sno = NULL;
	const EVP_MD    *digest = NULL;
	int             i = 0;
	//const char      *cert_subject = "This wants a subject";
	const char      *ext_name = "1.3.6.1.5.5.7.1.26";                   // our lovely TNAuthorizationList extension identifier that we will use to construct v3 extension of type TNAuthorizationList
	//const char    *ext_value = "DER:30:08:a0:06:16:04:37:38:36:35";   // TNAuthorizationList extension value, this is "asn1=SEQUENCE:tn_auth_list\ntn_auth_list]\nfield1=EXP:0,IA5:1237" in DER format
	char            ext_value_der[EXT_VALUE_DER_LEN] = {0};
	int             der_len = -1, include_der_string = 0;

	if (!req) {
		return NULL;
	}

	// Check csr
	if (!(pkey = X509_REQ_get_pubkey(req)))
		return NULL;

	i = X509_REQ_verify(req, pkey);
	if (i < 0)
		return NULL;
	EVP_PKEY_free(pkey);
	pkey = NULL;

	if ((x = X509_new()) == NULL) {
		return NULL;
	}

	sno = ASN1_INTEGER_new();
	X509_set_serialNumber(x, sno);
	ASN1_INTEGER_free(sno);
	sno = NULL;

	X509_set_issuer_name(x, X509_REQ_get_subject_name(req));
	X509_set_subject_name(x, X509_REQ_get_subject_name(req));
	X509_gmtime_adj(X509_get_notBefore(x), 0);
	X509_time_adj_ex(X509_get_notAfter(x), 365, 0, NULL);    // TODO days of expiration

	pkey = X509_REQ_get_pubkey(req);
	X509_set_pubkey(x, pkey);
	EVP_PKEY_free(pkey);
	pkey = NULL;

	// Add extensions to certificate
	X509_set_version(x, 2); // version 3 of certificate

	// Add extensions
	// DER
	include_der_string = 0;
	der_len = stir_shaken_generate_der(ext_value_der, EXT_VALUE_DER_LEN, sp_code, include_der_string);
	if (der_len == -1) {
		printf("STIR-Shaken: Cert ERR: Failed to generate DER\n");
		goto fail;
	}
	printf("STIR-Shaken: Cert: Got DER (len=%d): %s\n", der_len, ext_value_der);

	i = stir_shaken_v3_add_extensions(NULL, ext_name, ext_value_der, NULL, x);
	if (i == 0) {
		printf("STIR-Shaken: Cert ERR: Cannot load extensions into CSR\n");
		goto fail;
	}

	// Self sign
	if (!X509_sign(x, private_key, digest)) {
		printf("STIR-Shaken: Cert ERR: Failed to self sign certificate\n");
		goto fail;
	}
	printf("STIR-Shaken: Cert: Successfully self signed certificate\n");

	return x;

fail:
	return NULL;
}

stir_shaken_status_t stir_shaken_generate_cert_from_csr(uint32_t sp_code, stir_shaken_cert_t *cert, stir_shaken_csr_t *csr, EVP_PKEY *private_key, EVP_PKEY *public_key, const char *cert_full_name, const char *cert_text_full_name)
{
	X509            *x = NULL;
	BIO             *out = NULL, *bio = NULL;
	EVP_PKEY		*k = NULL;
	int i = 0;

	if (!csr) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	// Generate certificate

	if (!private_key || !public_key) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	x = stir_shaken_generate_x509_self_sign(sp_code, csr->req, private_key);
	if (!x) {
		return STIR_SHAKEN_STATUS_FALSE;
	}
	// TODO Maybe X509_set_pubkey(x, public_key); doesn't fix the leak though
	cert->x = x;

	if (cert_full_name) {
		out = BIO_new_fp(stdout, 0);
		i = BIO_write_filename(out, (char*) cert_full_name);
		if (i == 0) {
			// TODO set error string, allow for retrieval printf("STIR-Shaken: Cert ERR: Failed to redirect bio to file %s\n", cert_full_name);
			goto fail;
		}

		i = PEM_write_bio_X509(out, x);
		if (i == 0) {
			// TODO set error string, allow for retrieval: "STIR-Shaken: Cert ERR: Failed to write certificate to file %s\n", cert_full_name);
			goto fail;
		}

		// TODO set error string, allow for retrieval printf("STIR-Shaken: Cert: Written certificate to file %s\n", cert_full_name);
	}

	BIO_free_all(out);
	out = NULL;

	if (cert_text_full_name) {
		bio = BIO_new_file(cert_text_full_name, "w");
		if (!bio) goto anyway;

		// TODO This call leaks EVP_PKEY which it creates
		// ==17095==    at 0x4C28C20: malloc (vg_replace_malloc.c:296)
		// ==17095==    by 0x581D377: CRYPTO_malloc (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58B45D9: EVP_PKEY_new (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58C3C77: X509_PUBKEY_get (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58C8069: X509_print_ex (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x4E3BA66: stir_shaken_generate_cert_from_csr (stir_shaken_ssl.c:635)
		X509_print_ex(bio, x, XN_FLAG_COMPAT, X509_FLAG_COMPAT);

		BIO_free_all(bio);
		bio = NULL;

		// Need to decrement ref count of pubkey created by a call to X509_PUBKEY_get in X509_print_ex, so the pubkey is freed
		//k = X509_get0_pubkey(x);
		//EVP_PKEY_free(k);

		// TODO set error string, allow for retrieval printf("STIR-Shaken: Cert: Written certificate in human readable form to file %s\n", cert_text_full_name);
	}

anyway:
	if (bio) {
		BIO_free_all(bio);
	}
	if (out) {
		BIO_free_all(out);
	}
	return STIR_SHAKEN_STATUS_OK;

fail:
	if (bio) {
		BIO_free_all(bio);
	}
	if (out) {
		BIO_free_all(out);
	}
	if (x) {
		X509_free(x);
		x = NULL;
		cert->x = NULL;
	}
	return STIR_SHAKEN_STATUS_FALSE;
}

// TODO
// Robust version should read cert store to allow for cert/key file pack
//
// Something like
// X509_STORE  *cts = SSL_CTX_get_cert_store(ctx.native_handle());
// if(!cts || !cbio)
//		return false;
//		X509_INFO *itmp;
//		int i, count = 0, type = X509_FILETYPE_PEM;
//		STACK_OF(X509_INFO) *inf = PEM_X509_INFO_read_bio(cbio, NULL, NULL, NULL);
//		
//		iterate over all entries from the pem file, add them to the x509_store one by one
//
// see: https://stackoverflow.com/questions/3810058/read-certificate-files-from-memory-instead-of-a-file-using-openssl
//
stir_shaken_status_t stir_shaken_load_cert_from_mem(X509 **x, void *mem, size_t n)
{
	BIO *cbio = NULL;

	cbio = BIO_new_mem_buf(mem, -1);
	if (!cbio) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*x = PEM_read_bio_X509(cbio, NULL, 0, NULL);

	BIO_free(cbio);
	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_load_cert_from_file(X509 **x, const char *cert_tmp_name)
{
	BIO *in = NULL;

	in = BIO_new(BIO_s_file());
	if (!in) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (BIO_read_filename(in, cert_tmp_name) <= 0) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: Load: Cert ERR: Failed to load cert %s\n", cert_tmp_name);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*x = PEM_read_bio_X509(in, NULL, NULL, NULL);

	BIO_free(in);
	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_load_cert_and_key(const char *cert_name, stir_shaken_cert_t **cert, const char *private_key_name, EVP_PKEY **pkey)
{
	X509            *x = NULL;
	BIO             *in = NULL;
	char *b = NULL;

	if (!cert_name) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (!*cert) {
		*cert = malloc(sizeof(stir_shaken_cert_t));
		if (!*cert) {
			return STIR_SHAKEN_STATUS_FALSE;
		}
	}
	
	b = basename((char*) cert_name);

	// TODO need to free strings somewhere
	memset(*cert, 0, sizeof(stir_shaken_cert_t));
	(*cert)->full_name = malloc(strlen(cert_name) + 1);
	(*cert)->name = malloc(strlen(b) + 1);

	if (!(*cert)->name || !(*cert)->full_name) {
		return STIR_SHAKEN_STATUS_FALSE;
	}
	
	memcpy((*cert)->full_name, cert_name, strlen(cert_name));
	(*cert)->full_name[strlen(cert_name)] = '\0';

	memcpy((*cert)->name, b, strlen(b));
	(*cert)->name[strlen(b)] = '\0';

	if (stir_shaken_file_exists(cert_name) != STIR_SHAKEN_STATUS_OK) {
		
		// TODO remove
		printf("STIR-Shaken: File doesn't exist: %s\n", cert_name);
		free((*cert)->full_name);
		(*cert)->full_name = NULL;
		free((*cert)->name);
		(*cert)->name = NULL;
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (stir_shaken_file_exists(private_key_name) != STIR_SHAKEN_STATUS_OK) {
		
		// TODO remove
		printf("STIR-Shaken: File doesn't exist: %s\n", private_key_name);
		free((*cert)->full_name);
		(*cert)->full_name = NULL;
		free((*cert)->name);
		(*cert)->name = NULL;
		return STIR_SHAKEN_STATUS_FALSE;
	}

	in = BIO_new(BIO_s_file());
	if (BIO_read_filename(in, private_key_name) <= 0) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*pkey = PEM_read_bio_PrivateKey(in, NULL, NULL, NULL);
	BIO_free(in);


	in = BIO_new(BIO_s_file());
	if (!in) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (BIO_read_filename(in, cert_name) <= 0) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	x = PEM_read_bio_X509(in, NULL, NULL, NULL);
	(*cert)->x = x;
	(*cert)->pkey = *pkey;

	BIO_free(in);

	return STIR_SHAKEN_STATUS_OK;
}


stir_shaken_status_t stir_shaken_generate_keys(EC_KEY **eck, EVP_PKEY **priv, EVP_PKEY **pub, const char *private_key_full_name, const char *public_key_full_name)
{
	EC_KEY                  *ec_key = NULL;
	EVP_PKEY                *pk = NULL;
	BIO                     *out = NULL, *bio = NULL, *key = NULL;


	if (!stir_shaken_globals.initialised) {
		return STIR_SHAKEN_STATUS_RESTART;
	}

	if (eck == NULL || priv == NULL || pub == NULL || private_key_full_name == NULL || public_key_full_name == NULL)
		return STIR_SHAKEN_STATUS_FALSE;

	// file_remove(private_key_full_name, NULL);
	// file_remove(public_key_full_name, NULL);

	/* Generate EC key associated with our chosen curve. */
	ec_key = EC_KEY_new_by_curve_name(stir_shaken_globals.curve_nid);
	if (!ec_key) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Cannot construct new EC key\n");
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Got new EC\n");
	*eck = ec_key;

	if (!EC_KEY_generate_key(ec_key)) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Cannot generate new private/public keys from EC key\n");
		goto fail;
	}
	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Got new private/public EC key pair\n");

	if (!EC_KEY_check_key(ec_key)) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: EC key pair is invalid\n");
		goto fail;
	}
	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Private/public EC key pair is OK\n");

	// Print them out
	out = BIO_new_fp(stdout, BIO_NOCLOSE);
	PEM_write_bio_ECPrivateKey(out, ec_key, NULL, NULL, 0, NULL, NULL);
	PEM_write_bio_EC_PUBKEY(out, ec_key);
	BIO_free_all(out);
	out = NULL;

	// Save
	bio = BIO_new_file(private_key_full_name, "w");
	if (!bio) {
		goto fail;
	}
	PEM_write_bio_ECPrivateKey(bio, ec_key, NULL, NULL, 0, NULL, NULL);
	BIO_free_all(bio);
	bio = NULL;

	bio = BIO_new_file(public_key_full_name, "w");
	if (!bio) {
		goto fail;
	}
	PEM_write_bio_EC_PUBKEY(bio, ec_key);
	BIO_free_all(bio);
	bio = NULL;

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Saved private key: %s\n", private_key_full_name); printf("STIR-Shaken: SSL: Saved public key: %s\n", public_key_full_name);

	key = BIO_new(BIO_s_file());
	if (BIO_read_filename(key, private_key_full_name) <= 0) {
		// TODO set error string, allow for retrieval printf("Err, bio read priv key\n");
		goto fail;
	}
	pk = PEM_read_bio_PrivateKey(key, NULL, NULL, NULL);
	*priv = pk;

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Loaded pkey from: %s\n", private_key_full_name);
	BIO_free_all(key);

	key = BIO_new(BIO_s_file());
	if (BIO_read_filename(key, public_key_full_name) <= 0) {
		// TODO set error string, allow for retrieval printf("Err, bio read public key\n");
		goto fail;
	}
	pk = PEM_read_bio_PUBKEY(key, NULL, NULL, NULL);
	*pub = pk;

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Loaded pkey from: %s\n", public_key_full_name);

	BIO_free_all(key);
	BIO_free_all(out);
	BIO_free_all(bio);

	return STIR_SHAKEN_STATUS_OK;

fail:

	if (out) BIO_free_all(out);
	if (bio) BIO_free_all(bio);
	if (key) BIO_free_all(key);

	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_destroy_keys(EC_KEY **eck, EVP_PKEY **priv, EVP_PKEY **pub)
{
	if (eck && *eck) {
		EC_KEY_free(*eck);
		*eck = NULL;
	}
	if (priv && *priv) {
		EVP_PKEY_free(*priv);
		*priv = NULL;
	}
	if (pub && *pub) {
		EVP_PKEY_free(*pub);
		*pub = NULL;
	}
}

/**
 * Using @digest_name and @pkey create a signature for @data and save it in @out.
 * Return @out and length of it in @outlen.
 */ 
stir_shaken_status_t stir_shaken_do_sign_data_with_digest(const char *digest_name, EVP_PKEY *pkey, const char *data, size_t datalen, unsigned char *out, size_t *outlen)
{
	// TODO: JWS signature
	// JWS Signature = ES256(ASCII(BASE64URL(UTF8(JWS Protected Header)) || "." || BASE64URL(JWS Payload)))
	// JWS Signature = ES256(Main Signature)
	//
	//    +--------------+-------------------------------+--------------------+
	//    | "alg" Param  | Digital Signature or MAC      | Implementation     |
	//    | Value        | Algorithm                     | Requirements       |
	//    +--------------+-------------------------------+--------------------+
	//    | HS256        | HMAC using SHA-256            | Required           |
	//    | HS384        | HMAC using SHA-384            | Optional           |
	//    | HS512        | HMAC using SHA-512            | Optional           |
	//    | RS256        | RSASSA-PKCS1-v1_5 using       | Recommended        |
	//    |              | SHA-256                       |                    |
	//    | RS384        | RSASSA-PKCS1-v1_5 using       | Optional           |
	//    |              | SHA-384                       |                    |
	//    | RS512        | RSASSA-PKCS1-v1_5 using       | Optional           |
	//    |              | SHA-512                       |                    |
	//    | ES256        | ECDSA using P-256 and SHA-256 | Recommended+ 

	const EVP_MD    *md = NULL;
	EVP_MD_CTX      *mdctx = NULL;
	int             i = 0;


	if (!pkey || !data || !out || !outlen) return STIR_SHAKEN_STATUS_FALSE;

	md = EVP_get_digestbyname(digest_name);
	if (!md) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Cannot get %s digest\n", digest_name);
		goto err;
	}

	mdctx = EVP_MD_CTX_create();
	if (!mdctx) goto err;
	EVP_MD_CTX_init(mdctx);
	i = EVP_DigestSignInit(mdctx, NULL, md, NULL, pkey);
	if (i == 0) goto err;
	i = EVP_DigestSignUpdate(mdctx, data, datalen);
	if (i == 0) goto err;
	i = EVP_DigestSignFinal(mdctx, out, outlen);
	if (i == 0 || (*outlen >= PBUF_LEN - 1)) goto err;
	out[*outlen] = '\0';
	EVP_MD_CTX_destroy(mdctx);

	return STIR_SHAKEN_STATUS_OK;

err:
	return STIR_SHAKEN_STATUS_FALSE;
}

int stir_shaken_do_verify_data(const void *data, size_t datalen, const unsigned char *sig, size_t siglen, EVP_PKEY *public_key)
{
	BIO *bio_err = NULL;
	const EVP_MD    *md = NULL;
	EVP_MD_CTX *mctx = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	int r = -1;
	int res = -1;

	const char      *digest_name = "sha256";


	if (!data || !sig || siglen == 0 || !public_key) {
		goto err;
	}

	bio_err = BIO_new(BIO_s_file());
	BIO_set_fp(bio_err, stdout, BIO_NOCLOSE | BIO_FP_TEXT);

	md = EVP_get_digestbyname(digest_name);
	if (!md) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Cannot get %s digest\n", digest_name);
		goto err;
	}

	mctx = EVP_MD_CTX_create();
	//EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_MD_CTX_init(mctx);

	r = EVP_DigestVerifyInit(mctx, &pctx, md, NULL, public_key);
	if (r <= 0) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Error setting context\n");
		goto err;
	}

	r = EVP_DigestVerifyUpdate(mctx, (const void*)data, datalen);
	if (r <= 0) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Error updating context\n");
		goto err;
	}

	r = EVP_DigestVerifyFinal(mctx, (unsigned char*)sig, (unsigned int)siglen);
	if (r > 0) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Signature OK\n");
		res = 0;
	} else if (r == 0) {
		// TODO save error string and allow for retrieval by a caller:  sprintf(buf, "STIR-Shaken: Signature/data failed verification\n");
		res = 1;
	} else {
		// TODO save error string and allow for retrieval by a caller:  "STIR-Shaken: Error Verifying Data\n");
		res = 2;
		ERR_print_errors(bio_err);
	}

	if (mctx) {
		EVP_MD_CTX_destroy(mctx);
	}
	if (bio_err) {
		BIO_free(bio_err);
	}
	return res;

err:
	if (mctx) {
		EVP_MD_CTX_destroy(mctx);
	}
	if (bio_err) {
		BIO_free(bio_err);
	}
	return -1;
}

/**
 * Setup OpenSSL lib.
 * Must be called locked.
 */
stir_shaken_status_t stir_shaken_init_ssl(void)
{
	stir_shaken_settings_t  *settings = &stir_shaken_globals.settings;
	const SSL_METHOD        **ssl_method = &stir_shaken_globals.ssl_method;
	SSL_CTX                 **ssl_ctx = &stir_shaken_globals.ssl_ctx;
	SSL                     **ssl = &stir_shaken_globals.ssl;
	EC_builtin_curve        *curves = NULL, *c = NULL, *curve = NULL;
	size_t                  i = 0, n = 0;
	int                     curve_nid = -1;                 // id of the curve in OpenSSL


	if (stir_shaken_globals.initialised) {
		return STIR_SHAKEN_STATUS_NOOP;
	}

	// Init libssl
	SSL_library_init();
	SSL_load_error_strings();

	// Init libcrypto
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	*ssl_method = SSLv23_server_method();                   /* create server instance */

	ERR_clear_error();	
	*ssl_ctx = SSL_CTX_new(*ssl_method);                    /* create context */
	if (!*ssl_ctx) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Failed to init SSL context\n");
		fprintf(stderr, "STIR-Shaken: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL)); 
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*ssl = SSL_new(*ssl_ctx);
	if (!*ssl) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Failed to init SSL\n");
		return STIR_SHAKEN_STATUS_FALSE;
	}

	/* 1. Check if 256-bit eliptic curves are supported. */

	// Get total number of curves
	n = EC_get_builtin_curves(NULL, 0);
	if (n < 1) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Eliptic curves are not supported (change OpenSSL version to 1.0.2?)\n");
		goto fail;
	}
	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Eliptic curves supported (%zu)\n", n);

	/* 2. Check support for "X9.62/SECG curve over a 256 bit prime field" */

	// Get curves description
	curves = malloc(n * sizeof(EC_builtin_curve));									// TODO free
	if (!curves) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Not enough memory\n");
		goto fail;
	}
	EC_get_builtin_curves(curves, n);

	// Search for "prime256v1" curve
	for (i = 0; i < n; ++i) {
		c = &curves[i];
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Eliptic curve %s [%d] is supported\n", c->comment, c->nid);
		if (strstr(c->comment, "X9.62/SECG curve over a 256 bit prime field")) {
			curve_nid = c->nid;
			curve = c;
		}
	}

	if (curve_nid == -1 || !curve) {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL ERR: Eliptic curve 'X9.62/SECG curve over a 256 bit prime field' is not supported (change OpenSSL version to 1.0.2?)\n");
		goto fail;
	} else {
		// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Using (%s [%d]) eliptic curve\n", curve->comment, curve->nid);
	}
	stir_shaken_globals.curve_nid = curve_nid;

	free(curves);

	return STIR_SHAKEN_STATUS_OK;

fail:

	if (curves) free(curves);

	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_deinit_ssl(void)
{
	SSL_CTX **ssl_ctx = &stir_shaken_globals.ssl_ctx;
	SSL     **ssl = &stir_shaken_globals.ssl;

	if (*ssl) {
		SSL_free(*ssl);
		*ssl = NULL;
	}

	if (*ssl_ctx) {
		SSL_CTX_free(*ssl_ctx);
		*ssl_ctx = NULL;
	}
}