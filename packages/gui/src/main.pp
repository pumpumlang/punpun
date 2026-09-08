@inject->c("""
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int64_t pp_gui_available(void){return 1;}
int64_t pp_gui_message(const char *title,const char *message){return MessageBoxA(NULL,message,title,MB_OK|MB_ICONINFORMATION)==IDOK;}
#else
#include <dlfcn.h>
#include <X11/Xlib.h>
typedef Display*(*x_open_t)(const char*); typedef int(*x_close_t)(Display*); typedef int(*x_default_screen_t)(Display*);
typedef Window(*x_root_t)(Display*,int); typedef Window(*x_create_t)(Display*,Window,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long);
typedef int(*x_store_t)(Display*,Window,const char*); typedef int(*x_select_t)(Display*,Window,long); typedef int(*x_map_t)(Display*,Window);
typedef int(*x_next_t)(Display*,XEvent*); typedef int(*x_destroy_t)(Display*,Window); typedef int(*x_flush_t)(Display*);
static void *lib=NULL; static x_open_t xo; static x_close_t xc; static x_default_screen_t xs; static x_root_t xr; static x_create_t xcreate; static x_store_t xstore; static x_select_t xselect; static x_map_t xmap; static x_next_t xnext; static x_destroy_t xdestroy; static x_flush_t xflush;
static int loadx(void){if(xo)return 1;lib=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);if(!lib)return 0;
#define S(v,n) do{*(void**)(&v)=dlsym(lib,n);if(!(v))return 0;}while(0)
S(xo,"XOpenDisplay");S(xc,"XCloseDisplay");S(xs,"XDefaultScreen");S(xr,"XRootWindow");S(xcreate,"XCreateSimpleWindow");S(xstore,"XStoreName");S(xselect,"XSelectInput");S(xmap,"XMapWindow");S(xnext,"XNextEvent");S(xdestroy,"XDestroyWindow");S(xflush,"XFlush");
#undef S
return 1;}
int64_t pp_gui_available(void){if(!loadx())return 0;Display*d=xo(NULL);if(!d)return 0;xc(d);return 1;}
int64_t pp_gui_message(const char *title,const char *message){(void)message;if(!loadx())return 0;Display*d=xo(NULL);if(!d)return 0;int s=xs(d);Window w=xcreate(d,xr(d,s),40,40,520,180,1,0,0x202020);xstore(d,w,title?title:"PunPun");xselect(d,w,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);xmap(d,w);xflush(d);XEvent e;int done=0;while(!done){xnext(d,&e);if(e.type==KeyPress||e.type==ButtonPress||e.type==DestroyNotify)done=1;}xdestroy(d,w);xc(d);return 1;}
#endif
""");
extern native fn pp_gui_available() -> i64;
extern native fn pp_gui_message(title: String, message: String) -> i64;

fn gui_available() -> bool { return pp_gui_available() == 1; }
fn gui_message(title: String, message: String) -> bool { return pp_gui_message(title, message) == 1; }
