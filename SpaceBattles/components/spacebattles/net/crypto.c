// SPDX-License-Identifier: MIT
#include "net/crypto.h"
#include <string.h>
void sb_net_wipe(void *p, size_t n) { volatile uint8_t *v=p; while(n--) *v++=0; }
bool sb_net_equal(const void *a, const void *b, size_t n) {
    const uint8_t *x=a,*y=b; unsigned diff=0;
    for(size_t i=0;i<n;i++) diff|=x[i]^y[i];
    return diff==0;
}
static const uint8_t info[]="SpaceBattles/session/v1";
#ifdef SB_NET_HOST
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
bool sb_net_random(void *p,size_t n) { return n<=INT32_MAX && RAND_bytes(p,(int)n)==1; }
bool sb_net_hash(const void *p,size_t n,uint8_t out[32]) {
    unsigned len=0; return EVP_Digest(p,n,out,&len,EVP_sha256(),NULL)==1 && len==32;
}
bool sb_net_keygen(sb_net_key_t *k) {
    memset(k,0,sizeof(*k));
    EVP_PKEY *p=EVP_PKEY_Q_keygen(NULL,NULL,"EC","prime256v1");
    if(!p) return false;
    size_t n=0; int len=i2d_PrivateKey(p,NULL); unsigned char *dst=k->secret;
    bool ok=len>0 && len<=(int)sizeof(k->secret) && i2d_PrivateKey(p,&dst)==len &&
        EVP_PKEY_get_octet_string_param(p,OSSL_PKEY_PARAM_PUB_KEY,k->public_key,65,&n)==1 && n==65;
    k->secret_size=ok ? (size_t)len : 0; EVP_PKEY_free(p); return ok;
}
bool sb_net_shared(const sb_net_key_t *k,const uint8_t peer[65],uint8_t out[32]) {
    const unsigned char *src=k->secret;
    EVP_PKEY *local=d2i_AutoPrivateKey(NULL,&src,(long)k->secret_size),*remote=NULL;
    EVP_PKEY_CTX *import=EVP_PKEY_CTX_new_from_name(NULL,"EC",NULL),*derive=NULL;
    char group[]="prime256v1";
    OSSL_PARAM params[]={OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,group,0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,(void *)peer,65),OSSL_PARAM_construct_end()};
    bool ok=local && import && EVP_PKEY_fromdata_init(import)==1 &&
        EVP_PKEY_fromdata(import,&remote,EVP_PKEY_PUBLIC_KEY,params)==1;
    size_t n=32;
    if(ok) { derive=EVP_PKEY_CTX_new(local,NULL); ok=derive && EVP_PKEY_derive_init(derive)==1 &&
        EVP_PKEY_derive_set_peer(derive,remote)==1 && EVP_PKEY_derive(derive,out,&n)==1 && n==32; }
    EVP_PKEY_CTX_free(derive); EVP_PKEY_CTX_free(import); EVP_PKEY_free(local); EVP_PKEY_free(remote);
    return ok;
}
bool sb_net_kdf(const uint8_t shared[32],const uint8_t transcript[32],uint8_t out[72]) {
    EVP_PKEY_CTX *c=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL); size_t n=72;
    bool ok=c && EVP_PKEY_derive_init(c)==1 && EVP_PKEY_CTX_set_hkdf_md(c,EVP_sha256())==1 &&
        EVP_PKEY_CTX_set1_hkdf_salt(c,transcript,32)==1 && EVP_PKEY_CTX_set1_hkdf_key(c,shared,32)==1 &&
        EVP_PKEY_CTX_add1_hkdf_info(c,info,sizeof(info)-1)==1 && EVP_PKEY_derive(c,out,&n)==1 && n==72;
    EVP_PKEY_CTX_free(c); return ok;
}
bool sb_net_aead(bool enc,const uint8_t key[16],const uint8_t nonce[12],const uint8_t *aad,size_t an,
                 const uint8_t *in,size_t n,uint8_t *out,uint8_t tag[16]) {
    EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new(); int len=0,total=0;
    bool ok=c && EVP_CipherInit_ex(c,EVP_aes_128_gcm(),NULL,key,nonce,enc)==1 &&
        EVP_CipherUpdate(c,NULL,&len,aad,(int)an)==1 && EVP_CipherUpdate(c,out,&len,in,(int)n)==1;
    total=len;
    if(ok && !enc) ok=EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_SET_TAG,16,tag)==1;
    if(ok) ok=EVP_CipherFinal_ex(c,out+total,&len)==1;
    if(ok && enc) ok=EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_GET_TAG,16,tag)==1;
    EVP_CIPHER_CTX_free(c); if(!ok) sb_net_wipe(out,n);
    return ok;
}
#else
#include "psa/crypto.h"
bool sb_net_random(void *p,size_t n) { return psa_crypto_init()==PSA_SUCCESS && psa_generate_random(p,n)==PSA_SUCCESS; }
bool sb_net_hash(const void *p,size_t n,uint8_t out[32]) {
    size_t len=0; return psa_crypto_init()==PSA_SUCCESS && psa_hash_compute(PSA_ALG_SHA_256,p,n,out,32,&len)==PSA_SUCCESS && len==32;
}
bool sb_net_keygen(sb_net_key_t *k) {
    memset(k,0,sizeof(*k)); if(psa_crypto_init()!=PSA_SUCCESS) return false;
    psa_key_attributes_t a=PSA_KEY_ATTRIBUTES_INIT; psa_key_id_t id=0; size_t n=0;
    psa_set_key_type(&a,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)); psa_set_key_bits(&a,256);
    psa_set_key_usage_flags(&a,PSA_KEY_USAGE_EXPORT|PSA_KEY_USAGE_DERIVE); psa_set_key_algorithm(&a,PSA_ALG_ECDH);
    bool ok=psa_generate_key(&a,&id)==PSA_SUCCESS &&
        psa_export_key(id,k->secret,sizeof(k->secret),&k->secret_size)==PSA_SUCCESS &&
        psa_export_public_key(id,k->public_key,65,&n)==PSA_SUCCESS && n==65;
    psa_destroy_key(id); psa_reset_key_attributes(&a); return ok;
}
bool sb_net_shared(const sb_net_key_t *k,const uint8_t peer[65],uint8_t out[32]) {
    psa_key_attributes_t a=PSA_KEY_ATTRIBUTES_INIT; psa_key_id_t id=0; size_t n=0;
    psa_set_key_type(&a,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)); psa_set_key_bits(&a,256);
    psa_set_key_usage_flags(&a,PSA_KEY_USAGE_DERIVE); psa_set_key_algorithm(&a,PSA_ALG_ECDH);
    bool ok=psa_import_key(&a,k->secret,k->secret_size,&id)==PSA_SUCCESS &&
        psa_raw_key_agreement(PSA_ALG_ECDH,id,peer,65,out,32,&n)==PSA_SUCCESS && n==32;
    psa_destroy_key(id); psa_reset_key_attributes(&a); return ok;
}
bool sb_net_kdf(const uint8_t shared[32],const uint8_t transcript[32],uint8_t out[72]) {
    psa_key_derivation_operation_t op=PSA_KEY_DERIVATION_OPERATION_INIT;
    bool ok=psa_key_derivation_setup(&op,PSA_ALG_HKDF(PSA_ALG_SHA_256))==PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_SALT,transcript,32)==PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_SECRET,shared,32)==PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_INFO,info,sizeof(info)-1)==PSA_SUCCESS &&
        psa_key_derivation_output_bytes(&op,out,72)==PSA_SUCCESS;
    psa_key_derivation_abort(&op); return ok;
}
bool sb_net_aead(bool enc,const uint8_t key[16],const uint8_t nonce[12],const uint8_t *aad,size_t an,
                 const uint8_t *in,size_t n,uint8_t *out,uint8_t tag[16]) {
    if(n>253) return false;
    psa_key_attributes_t a=PSA_KEY_ATTRIBUTES_INIT; psa_key_id_t id=0; uint8_t buf[269]; size_t len=0;
    psa_set_key_type(&a,PSA_KEY_TYPE_AES); psa_set_key_bits(&a,128);
    psa_set_key_usage_flags(&a,enc ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT); psa_set_key_algorithm(&a,PSA_ALG_GCM);
    bool ok=psa_import_key(&a,key,16,&id)==PSA_SUCCESS;
    if(ok && enc) {
        ok=psa_aead_encrypt(id,PSA_ALG_GCM,nonce,12,aad,an,in,n,buf,sizeof(buf),&len)==PSA_SUCCESS && len==n+16;
        if(ok) { memcpy(out,buf,n); memcpy(tag,buf+n,16); }
    } else if(ok) {
        memcpy(buf,in,n); memcpy(buf+n,tag,16);
        ok=psa_aead_decrypt(id,PSA_ALG_GCM,nonce,12,aad,an,buf,n+16,out,n,&len)==PSA_SUCCESS && len==n;
    }
    psa_destroy_key(id); psa_reset_key_attributes(&a); sb_net_wipe(buf,sizeof(buf));
    if(!ok) sb_net_wipe(out,n);
    return ok;
}
#endif
