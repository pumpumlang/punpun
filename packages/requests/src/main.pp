@inject->c("""
#include <dlfcn.h>
#include "punpun.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The requests package loads libcurl at runtime, so requiring libcurl's
 * development headers made the otherwise portable package fail on clean CI
 * runners. These public ABI constants have been stable since the respective
 * options were introduced. Keeping the tiny ABI surface here means users need
 * the libcurl runtime, not a compiler-specific -dev package.
 */
typedef void CURL;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;
struct curl_slist;

#define CURLE_OK 0
#define CURL_GLOBAL_DEFAULT 3L
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_URL 10002
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_USERAGENT 10018
#define CURLOPT_HTTPHEADER 10023
#define CURLOPT_CUSTOMREQUEST 10036
#define CURLOPT_NOBODY 44
#define CURLOPT_POST 47
#define CURLOPT_FOLLOWLOCATION 52
#define CURLOPT_MAXREDIRS 68
#define CURLOPT_TIMEOUT_MS 155
#define CURLOPT_CONNECTTIMEOUT_MS 156
#define CURLINFO_RESPONSE_CODE 0x200002

typedef CURL *(*pp_curl_easy_init_t)(void);
typedef CURLcode (*pp_curl_easy_setopt_t)(CURL *, CURLoption, ...);
typedef CURLcode (*pp_curl_easy_perform_t)(CURL *);
typedef CURLcode (*pp_curl_easy_getinfo_t)(CURL *, CURLINFO, ...);
typedef void (*pp_curl_easy_cleanup_t)(CURL *);
typedef const char *(*pp_curl_easy_strerror_t)(CURLcode);
typedef CURLcode (*pp_curl_global_init_t)(long);
typedef struct curl_slist *(*pp_curl_slist_append_t)(struct curl_slist *, const char *);
typedef void (*pp_curl_slist_free_all_t)(struct curl_slist *);

typedef struct { char *data; size_t size; } pp_req_buffer;
static void *pp_curl_lib = NULL;
static pp_curl_easy_init_t p_init = NULL;
static pp_curl_easy_setopt_t p_setopt = NULL;
static pp_curl_easy_perform_t p_perform = NULL;
static pp_curl_easy_getinfo_t p_getinfo = NULL;
static pp_curl_easy_cleanup_t p_cleanup = NULL;
static pp_curl_easy_strerror_t p_strerror = NULL;
static pp_curl_global_init_t p_global_init = NULL;
static pp_curl_slist_append_t p_slist_append = NULL;
static pp_curl_slist_free_all_t p_slist_free_all = NULL;
#ifdef _MSC_VER
static __declspec(thread) long pp_req_last_status = 0;
static __declspec(thread) char pp_req_last_error[512] = "";
#else
static _Thread_local long pp_req_last_status = 0;
static _Thread_local char pp_req_last_error[512] = "";
#endif

static int pp_req_load(void) {
    if (p_init) return 1;
    const char *names[] = {"libcurl.so.4", "libcurl.so", NULL};
    for (int i=0; names[i] && !pp_curl_lib; ++i) pp_curl_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!pp_curl_lib) { snprintf(pp_req_last_error,sizeof(pp_req_last_error),"libcurl is unavailable: %s", dlerror()); return 0; }
#define PP_SYM(field,name) do { *(void**)(&field)=dlsym(pp_curl_lib,name); if (!(field)) { snprintf(pp_req_last_error,sizeof(pp_req_last_error),"libcurl symbol %s unavailable",name); return 0; } } while(0)
    PP_SYM(p_init,"curl_easy_init"); PP_SYM(p_setopt,"curl_easy_setopt");
    PP_SYM(p_perform,"curl_easy_perform"); PP_SYM(p_getinfo,"curl_easy_getinfo");
    PP_SYM(p_cleanup,"curl_easy_cleanup"); PP_SYM(p_strerror,"curl_easy_strerror");
    PP_SYM(p_global_init,"curl_global_init");
    PP_SYM(p_slist_append,"curl_slist_append"); PP_SYM(p_slist_free_all,"curl_slist_free_all");
#undef PP_SYM
    if (p_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { snprintf(pp_req_last_error,sizeof(pp_req_last_error),"curl global initialization failed"); return 0; }
    return 1;
}

static size_t pp_req_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
    pp_req_buffer *buffer=(pp_req_buffer*)userdata; size_t n=size*nmemb;
    if (n > SIZE_MAX-buffer->size-1) return 0;
    char *next=(char*)realloc(buffer->data,buffer->size+n+1); if (!next) return 0;
    buffer->data=next; memcpy(buffer->data+buffer->size,ptr,n); buffer->size+=n; buffer->data[buffer->size]='\0'; return n;
}

static struct curl_slist *pp_req_headers(const char *headers) {
    if (!headers || !*headers) return NULL;
    const size_t length=strlen(headers);
    char *copy=(char*)malloc(length+1); if (!copy) return NULL;
    memcpy(copy,headers,length+1);
    struct curl_slist *list=NULL;
    char *line=copy;
    for (char *cursor=copy;;++cursor) {
        if (*cursor!='\n' && *cursor!='\0') continue;
        const int done=*cursor=='\0'; *cursor='\0';
        size_t n=strlen(line); if (n && line[n-1]=='\r') line[n-1]='\0';
        if (*line) {
            struct curl_slist *next=p_slist_append(list,line);
            if (!next) { p_slist_free_all(list); free(copy); return NULL; }
            list=next;
        }
        if (done) break;
        line=cursor+1;
    }
    free(copy); return list;
}

static const char *pp_req_do(const char *method, const char *url, const char *body,
                             const char *headers, int64_t timeout_ms, int64_t follow) {
    pp_req_last_status=0; pp_req_last_error[0]='\0';
    if (!method || !url || timeout_ms<=0 || !pp_req_load()) {
        if (timeout_ms<=0) snprintf(pp_req_last_error,sizeof(pp_req_last_error),"timeout must be positive");
        return "";
    }
    CURL *curl=p_init(); if (!curl) { snprintf(pp_req_last_error,sizeof(pp_req_last_error),"curl_easy_init failed"); return ""; }
    pp_req_buffer buffer={NULL,0};
    struct curl_slist *header_list=pp_req_headers(headers);
    if (headers && *headers && !header_list) { p_cleanup(curl); snprintf(pp_req_last_error,sizeof(pp_req_last_error),"cannot allocate request headers"); return ""; }
    p_setopt(curl,CURLOPT_URL,url); p_setopt(curl,CURLOPT_FOLLOWLOCATION,follow?1L:0L); p_setopt(curl,CURLOPT_MAXREDIRS,10L);
    p_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,(long)(timeout_ms<10000?timeout_ms:10000)); p_setopt(curl,CURLOPT_TIMEOUT_MS,(long)timeout_ms);
    p_setopt(curl,CURLOPT_USERAGENT,"PunPun-requests/0.2"); p_setopt(curl,CURLOPT_WRITEFUNCTION,pp_req_write); p_setopt(curl,CURLOPT_WRITEDATA,&buffer);
    if (header_list) p_setopt(curl,CURLOPT_HTTPHEADER,header_list);
    if (strcmp(method,"HEAD")==0) p_setopt(curl,CURLOPT_NOBODY,1L);
    else if (strcmp(method,"POST")==0) { p_setopt(curl,CURLOPT_POST,1L); p_setopt(curl,CURLOPT_POSTFIELDS,body?body:""); }
    else if (strcmp(method,"GET")!=0) { p_setopt(curl,CURLOPT_CUSTOMREQUEST,method); if (body) p_setopt(curl,CURLOPT_POSTFIELDS,body); }
    CURLcode code=p_perform(curl);
    if (code!=CURLE_OK) snprintf(pp_req_last_error,sizeof(pp_req_last_error),"%s",p_strerror(code));
    p_getinfo(curl,CURLINFO_RESPONSE_CODE,&pp_req_last_status); if (header_list) p_slist_free_all(header_list); p_cleanup(curl);
    if (!buffer.data) { buffer.data=(char*)malloc(1); if (buffer.data) buffer.data[0]='\0'; }
    return buffer.data ? pp_adopt_text(buffer.data) : "";
}
const char *pp_requests_request(const char *method,const char *url,const char *body,const char *headers,int64_t timeout_ms,int64_t follow) {
    return pp_req_do(method,url,body,headers,timeout_ms,follow);
}
int64_t pp_requests_status(void) { return (int64_t)pp_req_last_status; }
const char *pp_requests_error(void) { return pp_req_last_error; }
int64_t pp_requests_available(void) { return pp_req_load() ? 1 : 0; }
""");

extern native fn pp_requests_request(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow: i64) -> String;
extern native fn pp_requests_status() -> i64;
extern native fn pp_requests_error() -> String;
extern native fn pp_requests_available() -> i64;

object HttpResponse {
    public let status: i64;
    public let body: String;
    public let error: String;

    public init(status: i64, body: String, error: String) {
        self.status = status;
        self.body = body;
        self.error = error;
    }

    public fn ok() -> bool { return self.status >= 200 && self.status < 300 && len(self.error) == 0; }
    public fn text() -> String { return self.body; }
}

fn requests_available() -> bool { return pp_requests_available() == 1; }
fn requests_request(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow_redirects: bool) -> HttpResponse {
    let mut follow = 0;
    if follow_redirects { follow = 1; }
    let response_body = pp_requests_request(method, url, body, headers, timeout_ms, follow);
    return HttpResponse(pp_requests_status(), response_body, pp_requests_error());
}
fn requests_get(url: String) -> HttpResponse {
    return requests_request("GET", url, "", "", 30000, true);
}
fn requests_post(url: String, body: String) -> HttpResponse {
    return requests_request("POST", url, body, "", 30000, true);
}
fn requests_put(url: String, body: String) -> HttpResponse { return requests_request("PUT", url, body, "", 30000, true); }
fn requests_patch(url: String, body: String) -> HttpResponse { return requests_request("PATCH", url, body, "", 30000, true); }
fn requests_delete(url: String) -> HttpResponse { return requests_request("DELETE", url, "", "", 30000, true); }
fn requests_head(url: String) -> HttpResponse { return requests_request("HEAD", url, "", "", 30000, true); }

# Async networking helpers. They reuse the same requests implementation inside
# native PunPun tasks, so callers can compose network work with task groups and
# cancellation without introducing a second HTTP stack.
async fn requests_request_async(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow_redirects: bool) -> HttpResponse {
    return requests_request(method, url, body, headers, timeout_ms, follow_redirects);
}
async fn requests_get_async(url: String) -> HttpResponse { return requests_get(url); }
async fn requests_post_async(url: String, body: String) -> HttpResponse { return requests_post(url, body); }
async fn requests_put_async(url: String, body: String) -> HttpResponse { return requests_put(url, body); }
async fn requests_patch_async(url: String, body: String) -> HttpResponse { return requests_patch(url, body); }
async fn requests_delete_async(url: String) -> HttpResponse { return requests_delete(url); }
async fn requests_head_async(url: String) -> HttpResponse { return requests_head(url); }

