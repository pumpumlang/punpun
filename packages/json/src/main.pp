@inject->c("""
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *jskip(const char *p){ while(*p && isspace((unsigned char)*p)) ++p; return p; }
static const char *jstring(const char *p){ if(*p!='\"') return NULL; ++p; while(*p){ if(*p=='\\'){ ++p; if(!*p) return NULL; ++p; continue; } if(*p=='\"') return p+1; if((unsigned char)*p<0x20) return NULL; ++p; } return NULL; }
static const char *jvalue(const char *p,int depth);
static const char *jarray(const char *p,int depth){ if(depth>128||*p!='[') return NULL; p=jskip(p+1); if(*p==']') return p+1; for(;;){ p=jvalue(p,depth+1); if(!p)return NULL; p=jskip(p); if(*p==']')return p+1; if(*p!=',')return NULL; p=jskip(p+1);} }
static const char *jobject(const char *p,int depth){ if(depth>128||*p!='{') return NULL; p=jskip(p+1); if(*p=='}') return p+1; for(;;){ p=jstring(p); if(!p)return NULL; p=jskip(p); if(*p!=':')return NULL; p=jskip(p+1); p=jvalue(p,depth+1); if(!p)return NULL; p=jskip(p); if(*p=='}')return p+1; if(*p!=',')return NULL; p=jskip(p+1);} }
static const char *jnumber(const char *p){ const char *s=p; if(*p=='-')++p; if(*p=='0')++p; else { if(!isdigit((unsigned char)*p))return NULL; while(isdigit((unsigned char)*p))++p; } if(*p=='.'){++p;if(!isdigit((unsigned char)*p))return NULL;while(isdigit((unsigned char)*p))++p;} if(*p=='e'||*p=='E'){++p;if(*p=='+'||*p=='-')++p;if(!isdigit((unsigned char)*p))return NULL;while(isdigit((unsigned char)*p))++p;} return p>s?p:NULL; }
static const char *jvalue(const char *p,int depth){ p=jskip(p); if(*p=='\"')return jstring(p); if(*p=='{')return jobject(p,depth); if(*p=='[')return jarray(p,depth); if(!strncmp(p,"true",4))return p+4; if(!strncmp(p,"false",5))return p+5; if(!strncmp(p,"null",4))return p+4; return jnumber(p); }
int64_t pp_json_valid(const char *text){ if(!text)return 0; const char *end=jvalue(text,0); return end && *jskip(end)=='\0'; }
static const char *find_key(const char *json,const char *key){ if(!json||!key)return NULL; const char *p=jskip(json); if(*p!='{')return NULL; p=jskip(p+1); while(*p && *p!='}'){ const char *ks=p; const char *ke=jstring(ks); if(!ke)return NULL; size_t n=(size_t)((ke-1)-(ks+1)); p=jskip(ke); if(*p!=':')return NULL; p=jskip(p+1); if(strlen(key)==n && !strncmp(ks+1,key,n)) return p; p=jvalue(p,0); if(!p)return NULL; p=jskip(p); if(*p==',')p=jskip(p+1); else break; } return NULL; }
const char *pp_json_get_string(const char *json,const char *key,const char *fallback){ const char *p=find_key(json,key); if(!p||*p!='\"')return fallback; const char *e=jstring(p); if(!e)return fallback; size_t n=(size_t)((e-1)-(p+1)); char *out=(char*)malloc(n+1); if(!out)return fallback; memcpy(out,p+1,n); out[n]='\0'; return out; }
int64_t pp_json_get_i64(const char *json,const char *key,int64_t fallback){ const char *p=find_key(json,key); if(!p)return fallback; char *end=NULL; long long v=strtoll(p,&end,10); if(end==p)return fallback; return (int64_t)v; }
const char *pp_json_quote(const char *text){ if(!text)return "\"\""; size_t n=2; for(const unsigned char *p=(const unsigned char*)text;*p;++p)n+=(*p=='\"'||*p=='\\')?2:1; char *out=(char*)malloc(n+1); if(!out)return "\"\""; char *q=out;*q++='\"';for(const unsigned char *p=(const unsigned char*)text;*p;++p){if(*p=='\"'||*p=='\\')*q++='\\';*q++=(char)*p;}*q++='\"';*q='\0';return out; }
""");
extern native fn pp_json_valid(text: String) -> i64;
extern native fn pp_json_get_string(text: String, key: String, fallback: String) -> String;
extern native fn pp_json_get_i64(text: String, key: String, fallback: i64) -> i64;
extern native fn pp_json_quote(text: String) -> String;

fn json_valid(text: String) -> bool { return pp_json_valid(text) == 1; }
fn json_get_string(text: String, key: String, fallback: String) -> String { return pp_json_get_string(text, key, fallback); }
fn json_get_i64(text: String, key: String, fallback: i64) -> i64 { return pp_json_get_i64(text, key, fallback); }
fn json_quote(text: String) -> String { return pp_json_quote(text); }
