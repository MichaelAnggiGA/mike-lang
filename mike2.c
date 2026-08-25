/* ============================================================================
 * mike — a native compiler for the Mike language  (numeric edition)
 *
 *   mike prog.mik  ->  x86-64 assembly  ->  as  ->  ld  ->  ./prog
 *
 *   All values are IEEE-754 doubles. Built for numeric / statistical work:
 *     - floating point arithmetic (SSE / xmm registers)
 *     - arrays:            var a = array(100)   a[i] = x   print a[i]
 *     - math builtins:     sqrt exp ln pow sin cos abs floor  (via libm)
 *     - stochastic RNG:    seed(n)  random()  rand_range(lo,hi)  gauss(mu,sd)
 *     - keyboard input:    input "prompt"      (reads a number)
 *     - functions, recursion, if/else, while, for..to, print
 *     - raw escape hatch:  asm "..."
 *
 *   This edition links libm + the C runtime (for malloc + math), so the
 *   backend: generate assembly -> cc (gcc/clang) to assemble & link.
 *   Portable across x86-64 Unix (Linux, FreeBSD): all OS I/O goes
 *   through libc write/read, so no per-OS syscall numbers are needed.
 *   (The pure no-libc path from the previous edition still lives in mike.c.)
 *
 *   Build:  cc -O2 -o mike mike2.c
 *   Use:    ./mike prog.mik      # -> ./prog
 *           ./mike -S prog.mik   # keep prog.s
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <libgen.h>

/* ======================================================================== */
/*  Lexer                                                                    */
/* ======================================================================== */

typedef enum {
    T_NUMBER, T_STRING, T_IDENT,
    T_START, T_MODULE, T_MODUL, T_LIB, T_PRINT, T_VAR,
    T_FUNC, T_RETURN, T_END, T_IF, T_ELSE, T_WHILE, T_FOR, T_TO,
    T_AND, T_OR, T_NOT, T_ASM, T_INPUT,
    T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_COMMA, T_SEMI, T_SLASH,
    T_PLUS, T_MINUS, T_STAR, T_PERCENT,
    T_EQ, T_EQEQ, T_BANGEQ, T_LT, T_LTE, T_GT, T_GTE,
    T_EOF, T_ERROR
} TokType;

typedef struct {
    TokType type; const char *start; int length; int line; double number;
} Token;

typedef struct { const char *cur; int line; } Lexer;
static Lexer LX;
static void lexer_init(const char *src){ LX.cur=src; LX.line=1; }

static bool is_alpha(char c){ return isalpha((unsigned char)c)||c=='_'; }
static bool is_digit(char c){ return isdigit((unsigned char)c); }

static Token mk(TokType t,const char*s,int len){
    Token k; k.type=t; k.start=s; k.length=len; k.line=LX.line; k.number=0; return k;
}
static Token err_tok(const char*m){ return mk(T_ERROR,m,(int)strlen(m)); }

static void skip_ws(void){
    for(;;){ char c=*LX.cur;
        if(c==' '||c=='\t'||c=='\r') LX.cur++;
        else if(c=='\n'){ LX.line++; LX.cur++; }
        else if(c=='#'){ while(*LX.cur&&*LX.cur!='\n') LX.cur++; }
        else return;
    }
}
static TokType kw(const char*s,int n){
    struct{const char*k;TokType t;}K[]={
        {"start",T_START},{"module",T_MODULE},{"modul",T_MODUL},{"lib",T_LIB},
        {"print",T_PRINT},{"var",T_VAR},{"func",T_FUNC},{"return",T_RETURN},
        {"end",T_END},{"if",T_IF},{"else",T_ELSE},{"while",T_WHILE},
        {"for",T_FOR},{"to",T_TO},{"and",T_AND},{"or",T_OR},{"not",T_NOT},
        {"asm",T_ASM},{"input",T_INPUT},
    };
    for(unsigned i=0;i<sizeof(K)/sizeof(K[0]);i++)
        if((int)strlen(K[i].k)==n&&memcmp(K[i].k,s,n)==0) return K[i].t;
    return T_IDENT;
}
static Token lex_next(void){
    skip_ws();
    const char*s=LX.cur; char c=*LX.cur;
    if(c=='\0') return mk(T_EOF,s,0);
    LX.cur++;
    if(is_alpha(c)){
        while(is_alpha(*LX.cur)||is_digit(*LX.cur)) LX.cur++;
        int len=(int)(LX.cur-s); return mk(kw(s,len),s,len);
    }
    if(is_digit(c)||(c=='.'&&is_digit(*LX.cur))){
        while(is_digit(*LX.cur)) LX.cur++;
        if(*LX.cur=='.'){ LX.cur++; while(is_digit(*LX.cur)) LX.cur++; }
        if(*LX.cur=='e'||*LX.cur=='E'){ LX.cur++; if(*LX.cur=='+'||*LX.cur=='-')LX.cur++; while(is_digit(*LX.cur))LX.cur++; }
        Token t=mk(T_NUMBER,s,(int)(LX.cur-s)); t.number=strtod(s,NULL); return t;
    }
    if(c=='"'){
        const char*b=LX.cur;
        while(*LX.cur&&*LX.cur!='"'){
            if(*LX.cur=='\\' && LX.cur[1]!='\0') LX.cur++;
            if(*LX.cur=='\n')LX.line++;
            LX.cur++;
        }
        if(*LX.cur=='\0') return err_tok("unterminated string");
        Token t=mk(T_STRING,b,(int)(LX.cur-b)); LX.cur++; return t;
    }
    switch(c){
        case '(':return mk(T_LPAREN,s,1);
        case ')':return mk(T_RPAREN,s,1);
        case '[':return mk(T_LBRACKET,s,1);
        case ']':return mk(T_RBRACKET,s,1);
        case ',':return mk(T_COMMA,s,1);
        case ';':return mk(T_SEMI,s,1);
        case '+':return mk(T_PLUS,s,1);
        case '-':return mk(T_MINUS,s,1);
        case '*':return mk(T_STAR,s,1);
        case '%':return mk(T_PERCENT,s,1);
        case '/':return mk(T_SLASH,s,1);
        case '=':if(*LX.cur=='='){LX.cur++;return mk(T_EQEQ,s,2);} return mk(T_EQ,s,1);
        case '!':if(*LX.cur=='='){LX.cur++;return mk(T_BANGEQ,s,2);} return err_tok("unexpected '!'");
        case '<':if(*LX.cur=='='){LX.cur++;return mk(T_LTE,s,2);} return mk(T_LT,s,1);
        case '>':if(*LX.cur=='='){LX.cur++;return mk(T_GTE,s,2);} return mk(T_GT,s,1);
    }
    return err_tok("unexpected character");
}

/* ======================================================================== */
/*  Growable string                                                          */
/* ======================================================================== */

typedef struct { char*buf; size_t len,cap; } Str;
static void str_init(Str*s){ s->cap=4096; s->len=0; s->buf=malloc(s->cap); s->buf[0]='\0'; }
static void str_add(Str*s,const char*fmt,...){
    va_list a; va_start(a,fmt); va_list a2; va_copy(a2,a);
    int n=vsnprintf(NULL,0,fmt,a); va_end(a);
    if(n<0){ va_end(a2); return; }
    if(s->len+n+1>s->cap){ while(s->len+n+1>s->cap) s->cap*=2; s->buf=realloc(s->buf,s->cap); }
    vsnprintf(s->buf+s->len,n+1,fmt,a2); va_end(a2); s->len+=n;
}
static Str TEXT, DATA;

/* ---- library system ---- */
static char  g_libdir[512] = "lib";     /* where to find <name>.mik */
static char  g_loaded[32][64];          /* names already loaded (dedupe) */
static int   g_loaded_count;
static Str   g_prelude;                 /* concatenated library source */

static bool lib_already(const char*name,int len){
    for(int i=0;i<g_loaded_count;i++)
        if((int)strlen(g_loaded[i])==len && memcmp(g_loaded[i],name,len)==0) return true;
    return false;
}
static void lib_mark(const char*name,int len){
    int k=len<63?len:63; memcpy(g_loaded[g_loaded_count],name,k);
    g_loaded[g_loaded_count][k]='\0'; g_loaded_count++;
}

/* ======================================================================== */
/*  Symbols                                                                  */
/* ======================================================================== */

typedef struct { char name[64]; int offset; } Local;
typedef struct {
    char name[64]; int arity;
    Local locals[128]; int local_count; int frame_size;
} Func;
static Func funcs[128]; static int func_count; static Func*cur_func;

static int str_lit_count, label_count, dbl_lit_count;
static int next_label(void){ return label_count++; }

static Func*find_func(const char*n,int len){
    for(int i=0;i<func_count;i++)
        if((int)strlen(funcs[i].name)==len&&memcmp(funcs[i].name,n,len)==0) return &funcs[i];
    return NULL;
}
static Local*find_local(Func*f,const char*n,int len){
    for(int i=0;i<f->local_count;i++)
        if((int)strlen(f->locals[i].name)==len&&memcmp(f->locals[i].name,n,len)==0) return &f->locals[i];
    return NULL;
}
static Local*add_local(Func*f,const char*n,int len){
    Local*l=find_local(f,n,len); if(l) return l;
    l=&f->locals[f->local_count++];
    int k=len<63?len:63; memcpy(l->name,n,k); l->name[k]='\0';
    f->frame_size+=8; l->offset=-f->frame_size; return l;
}

/* ======================================================================== */
/*  Codegen — every expression leaves its double result in %xmm0             */
/* ======================================================================== */

static Token cur, prev; static bool had_error;
static void error(const char*m){ fprintf(stderr,"[line %d] error at '%.*s': %s\n",cur.line,cur.length,cur.start,m); had_error=true; }
static void advance(void){ prev=cur; for(;;){ cur=lex_next(); if(cur.type!=T_ERROR)break; error(cur.start);} }
static bool check(TokType t){ return cur.type==t; }
static bool match(TokType t){ if(!check(t))return false; advance(); return true; }
static void consume(TokType t,const char*m){ if(check(t)){advance();return;} error(m); }

static void emit(const char*fmt,...){ va_list a; va_start(a,fmt); char t[512]; vsnprintf(t,sizeof(t),fmt,a); va_end(a); str_add(&TEXT,"    %s\n",t); }
static void emit_label(const char*fmt,...){ va_list a; va_start(a,fmt); char t[128]; vsnprintf(t,sizeof(t),fmt,a); va_end(a); str_add(&TEXT,"%s:\n",t); }

/* a double constant -> label in .data, loaded into xmm0 */
static int add_double(double v){
    int id=dbl_lit_count++;
    /* emit raw 64-bit pattern so we reproduce the exact value */
    union { double d; unsigned long long u; } bits; bits.d=v;
    str_add(&DATA,"dbl_%d:\n    .quad 0x%016llx   # %g\n",id,bits.u,v);
    return id;
}
static int add_string(const char*s,int len){
    int id=str_lit_count++;
    str_add(&DATA,"str_%d:\n    .ascii \"",id);
    for(int i=0;i<len;i++){ char c=s[i];
        if(c=='\\'&&i+1<len){ char n=s[i+1];
            if(n=='n'){str_add(&DATA,"\\n");i++;continue;}
            if(n=='t'){str_add(&DATA,"\\t");i++;continue;}
            if(n=='"'){str_add(&DATA,"\\\"");i++;continue;}
            if(n=='\\'){str_add(&DATA,"\\\\");i++;continue;}
        }
        if(c=='"') str_add(&DATA,"\\\"");
        else if(c=='\n') str_add(&DATA,"\\n");
        else str_add(&DATA,"%c",c);
    }
    str_add(&DATA,"\\0\"\n    .set str_%d_len, . - str_%d - 1\n",id,id);
    return id;
}

static void expression(void);
static void statement(void);

/* push/pop a double on the CPU stack */
static void push_xmm(void){ emit("sub $8, %%rsp"); emit("movsd %%xmm0, (%%rsp)"); }
static void pop_into(const char*reg){ emit("movsd (%%rsp), %s",reg); emit("add $8, %%rsp"); }

/* forward: builtin dispatch */
static bool try_builtin(Token name);

static const char*ARG_XMM[8]={"%xmm0","%xmm1","%xmm2","%xmm3","%xmm4","%xmm5","%xmm6","%xmm7"};

static void call_user(Func*f){
    int argc=0;
    if(!check(T_RPAREN)){
        do{ expression(); push_xmm(); argc++; }while(match(T_COMMA));
    }
    consume(T_RPAREN,"expect ')'");
    if(argc!=f->arity) error("wrong number of arguments");
    for(int i=argc-1;i>=0;i--) pop_into(ARG_XMM[i]);
    emit("call %s",f->name);   /* result in xmm0 */
}

/* Forward-reference call: the target function isn't compiled yet (e.g. a
   library routine calling a user-provided f). We emit a call by name and
   trust it exists by link time. Arity can't be checked here. */
static void call_by_name(Token name){
    char nm[64]; int k=name.length<63?name.length:63;
    memcpy(nm,name.start,k); nm[k]='\0';
    int argc=0;
    if(!check(T_RPAREN)){
        do{ expression(); push_xmm(); argc++; }while(match(T_COMMA));
    }
    consume(T_RPAREN,"expect ')'");
    for(int i=argc-1;i>=0;i--) pop_into(ARG_XMM[i]);
    emit("call %s",nm);
}

static void primary(void){
    if(match(T_NUMBER)){
        int id=add_double(prev.number);
        emit("movsd dbl_%d(%%rip), %%xmm0",id);
        return;
    }
    if(match(T_LPAREN)){ expression(); consume(T_RPAREN,"expect ')'"); return; }
    if(match(T_MINUS)){
        primary();
        /* negate: xor sign bit */
        emit("movsd .Lsignmask(%%rip), %%xmm1");
        emit("xorpd %%xmm1, %%xmm0");
        return;
    }
    if(match(T_NOT)){
        primary();
        emit("xorpd %%xmm1, %%xmm1");
        emit("ucomisd %%xmm1, %%xmm0");
        emit("sete %%al");
        emit("movzbq %%al, %%rax");
        emit("cvtsi2sd %%rax, %%xmm0");
        return;
    }
    if(match(T_INPUT)){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("mov $1, %%rdi");
            emit("lea str_%d(%%rip), %%rsi",id); emit("mov $str_%d_len, %%rdx",id);
            emit("call write");
        }
        emit("call read_double");   /* -> xmm0 */
        return;
    }
    if(match(T_IDENT)){
        Token name=prev;
        if(check(T_LPAREN)){
            advance();
            if(try_builtin(name)) return;
            Func*f=find_func(name.start,name.length);
            if(!f){ call_by_name(name); return; }  /* forward reference */
            call_user(f);
            return;
        }
        /* variable, possibly indexed a[i] */
        Local*l=find_local(cur_func,name.start,name.length);
        if(!l){ error("undefined variable"); return; }
        if(match(T_LBRACKET)){
            /* array load: base pointer stored as double bits in the local */
            emit("movsd %d(%%rbp), %%xmm0",l->offset);
            emit("movq %%xmm0, %%r11");        /* r11 = base pointer */
            emit("push %%r11");
            expression();                       /* index -> xmm0 */
            consume(T_RBRACKET,"expect ']'");
            emit("cvttsd2si %%xmm0, %%rax");    /* index -> int */
            emit("pop %%r11");
            emit("movsd (%%r11,%%rax,8), %%xmm0");
            return;
        }
        emit("movsd %d(%%rbp), %%xmm0",l->offset);
        return;
    }
    error("expect expression");
}

static int prec_of(TokType t){
    switch(t){
        case T_OR:return 1; case T_AND:return 2;
        case T_EQEQ: case T_BANGEQ:return 3;
        case T_LT: case T_LTE: case T_GT: case T_GTE:return 4;
        case T_PLUS: case T_MINUS:return 5;
        case T_STAR: case T_SLASH: case T_PERCENT:return 6;
        default:return 0;
    }
}

/* comparison helper: sets xmm0 to 1.0/0.0 from an integer flag in al */
static void flag_to_double(void){
    emit("movzbq %%al, %%rax");
    emit("cvtsi2sd %%rax, %%xmm0");
}

static void binary_rhs(int min_prec){
    for(;;){
        int p=prec_of(cur.type); if(p<min_prec||p==0) return;
        TokType op=cur.type; advance();
        push_xmm();                 /* save left */
        primary();
        for(;;){ int p2=prec_of(cur.type); if(p2>p) binary_rhs(p2); else break; }
        emit("movsd %%xmm0, %%xmm1"); /* xmm1 = right */
        pop_into("%xmm0");            /* xmm0 = left  */
        switch(op){
            case T_PLUS:  emit("addsd %%xmm1, %%xmm0"); break;
            case T_MINUS: emit("subsd %%xmm1, %%xmm0"); break;
            case T_STAR:  emit("mulsd %%xmm1, %%xmm0"); break;
            case T_SLASH: emit("divsd %%xmm1, %%xmm0"); break;
            case T_PERCENT:
                /* fmod via truncation: a - trunc(a/b)*b */
                emit("movsd %%xmm0, %%xmm2");   /* xmm2=a */
                emit("divsd %%xmm1, %%xmm0");   /* a/b */
                emit("cvttsd2si %%xmm0, %%rax");
                emit("cvtsi2sd %%rax, %%xmm0"); /* trunc(a/b) */
                emit("mulsd %%xmm1, %%xmm0");   /* *b */
                emit("movsd %%xmm2, %%xmm3"); emit("subsd %%xmm0, %%xmm3");
                emit("movsd %%xmm3, %%xmm0"); break;
            case T_EQEQ:  emit("ucomisd %%xmm1, %%xmm0"); emit("sete %%al");  flag_to_double(); break;
            case T_BANGEQ:emit("ucomisd %%xmm1, %%xmm0"); emit("setne %%al"); flag_to_double(); break;
            case T_LT:    emit("ucomisd %%xmm1, %%xmm0"); emit("setb %%al");  flag_to_double(); break;
            case T_LTE:   emit("ucomisd %%xmm1, %%xmm0"); emit("setbe %%al"); flag_to_double(); break;
            case T_GT:    emit("ucomisd %%xmm1, %%xmm0"); emit("seta %%al");  flag_to_double(); break;
            case T_GTE:   emit("ucomisd %%xmm1, %%xmm0"); emit("setae %%al"); flag_to_double(); break;
            case T_AND: {
                emit("xorpd %%xmm2, %%xmm2");
                emit("ucomisd %%xmm2, %%xmm0"); emit("setne %%al");
                emit("ucomisd %%xmm2, %%xmm1"); emit("setne %%cl");
                emit("and %%cl, %%al"); flag_to_double(); break;
            }
            case T_OR: {
                emit("xorpd %%xmm2, %%xmm2");
                emit("ucomisd %%xmm2, %%xmm0"); emit("setne %%al");
                emit("ucomisd %%xmm2, %%xmm1"); emit("setne %%cl");
                emit("or %%cl, %%al"); flag_to_double(); break;
            }
            default: break;
        }
    }
}
static void expression(void){ primary(); binary_rhs(1); }

/* ---- builtins: math (libm) + rng + array + len ------------------------- */
/* Each consumes its args (already past '(') and leaves result in xmm0.     */

static void one_arg(void){ expression(); }
static void two_args(const char*errmsg){
    expression(); push_xmm();
    consume(T_COMMA,errmsg);
    expression();                 /* second arg in xmm0 */
    emit("movsd %%xmm0, %%xmm1"); /* xmm1 = 2nd */
    pop_into("%xmm0");            /* xmm0 = 1st */
}

static bool try_builtin(Token name){
    #define IS(s) ((int)strlen(s)==name.length && memcmp(s,name.start,name.length)==0)

    /* --- unary libm --- */
    const char*uni=NULL;
    if(IS("sqrt")) uni="sqrt";
    else if(IS("exp")) uni="exp";
    else if(IS("ln")) uni="log";
    else if(IS("log10")) uni="log10";
    else if(IS("sin")) uni="sin";
    else if(IS("cos")) uni="cos";
    else if(IS("tan")) uni="tan";
    else if(IS("floor")) uni="floor";
    else if(IS("ceil")) uni="ceil";
    if(uni){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("call %s",uni);   /* xmm0 -> xmm0 (System V) */
        return true;
    }
    if(IS("abs")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("movsd .Labsmask(%%rip), %%xmm1");
        emit("andpd %%xmm1, %%xmm0");
        return true;
    }
    if(IS("pow")){
        two_args("pow needs 2 args"); consume(T_RPAREN,"expect ')'");
        emit("call pow"); return true;
    }
    /* --- array(n): allocate n doubles, return pointer as double bits --- */
    if(IS("array")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("cvttsd2si %%xmm0, %%rdi");   /* count */
        emit("imul $8, %%rdi");            /* bytes */
        emit("call malloc");               /* rax = ptr */
        emit("movq %%rax, %%xmm0");        /* store pointer bits in a double */
        return true;
    }
    /* --- rng --- */
    if(IS("seed")){ one_arg(); consume(T_RPAREN,"expect ')'");
        emit("cvttsd2si %%xmm0, %%rdi"); emit("call rng_seed");
        emit("xorpd %%xmm0, %%xmm0"); return true; }
    if(IS("random")){ consume(T_RPAREN,"expect ')'"); emit("call rng_next"); return true; }
    if(IS("rand_range")){ two_args("rand_range needs 2 args"); consume(T_RPAREN,"expect ')'");
        emit("call rng_range"); return true; }
    if(IS("gauss")){ two_args("gauss needs 2 args"); consume(T_RPAREN,"expect ')'");
        emit("call rng_gauss"); return true; }

    /* --- csv_rows("file.csv") -> number of numeric values in file --- */
    if(IS("csv_rows")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("lea str_%d(%%rip), %%rdi",id);
        } else { error("csv_rows needs a filename string"); }
        consume(T_RPAREN,"expect ')'");
        emit("call csv_count");   /* -> xmm0 = count */
        return true;
    }
    /* --- csv_load("file.csv", arr) -> fills arr, returns count --- */
    if(IS("csv_load")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("lea str_%d(%%rip), %%rdi",id);
        } else { error("csv_load needs a filename string"); }
        emit("push %%rdi");
        consume(T_COMMA,"csv_load needs (\"file\", array)");
        expression();                     /* array pointer bits in xmm0 */
        emit("movq %%xmm0, %%rsi");        /* rsi = array base */
        emit("pop %%rdi");
        consume(T_RPAREN,"expect ')'");
        emit("call csv_load_rt");          /* -> xmm0 = count */
        return true;
    }

    /* --- file writing builtins --- */
    /* file_open("name") -> open file for writing (fopen "w") */
    if(IS("file_open")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("lea str_%d(%%rip), %%rdi",id);
        } else { error("file_open needs a filename string"); }
        consume(T_RPAREN,"expect ')'");
        emit("call file_open_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* file_str("text") -> write a string literal to the open file */
    if(IS("file_str")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("lea str_%d(%%rip), %%rdi",id);
            emit("mov $str_%d_len, %%rsi",id);
        } else { error("file_str needs a string"); }
        consume(T_RPAREN,"expect ')'");
        emit("call file_str_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* file_num(x) -> write a number to the open file */
    if(IS("file_num")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("call file_num_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* file_close() -> close the open file */
    if(IS("file_close")){
        consume(T_RPAREN,"expect ')'");
        emit("call file_close_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }

    /* --- putc(code): print one ASCII char (no newline) --- */
    if(IS("putc")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("cvttsd2si %%xmm0, %%rdi");   /* char code */
        emit("call putc_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* --- newline(): print a newline --- */
    if(IS("newline")){
        consume(T_RPAREN,"expect ')'");
        emit("call print_newline");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* --- putnum(x): print a number with NO trailing newline --- */
    if(IS("putnum")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("call print_double");    /* prints value, no newline */
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* --- putstr("text"): print a string literal with NO newline --- */
    if(IS("putstr")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("mov $1, %%rdi");
            emit("lea str_%d(%%rip), %%rsi",id); emit("mov $str_%d_len, %%rdx",id);
            emit("call write");
        } else { error("putstr needs a string literal"); }
        consume(T_RPAREN,"expect ')'");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }

    /* --- terminal output without newline (for ASCII plotting) --- */
    /* put_char(code): print one ASCII character given its code */
    if(IS("put_char")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("call put_char_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* put_str("text"): print a string literal, no newline */
    if(IS("put_str")){
        if(check(T_STRING)){ advance(); int id=add_string(prev.start,prev.length);
            emit("lea str_%d(%%rip), %%rdi",id);
            emit("mov $str_%d_len, %%rsi",id);
        } else { error("put_str needs a string"); }
        consume(T_RPAREN,"expect ')'");
        emit("call put_str_rt");
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }
    /* put_num(x): print a number, no newline */
    if(IS("put_num")){
        one_arg(); consume(T_RPAREN,"expect ')'");
        emit("call print_double");   /* prints value in xmm0, no newline */
        emit("xorpd %%xmm0, %%xmm0");
        return true;
    }

    #undef IS
    return false;
}

/* ======================================================================== */
/*  print — string literal, or numeric expression                           */
/* ======================================================================== */

static void print_statement(void){
    if(check(T_STRING)){
        advance(); int id=add_string(prev.start,prev.length);
        emit("mov $1, %%rdi");
        emit("lea str_%d(%%rip), %%rsi",id); emit("mov $str_%d_len, %%rdx",id);
        emit("call write"); emit("call print_newline");
    } else {
        expression();               /* xmm0 = value */
        emit("call print_double");
        emit("call print_newline");
    }
}

/* ======================================================================== */
/*  Statements                                                               */
/* ======================================================================== */

static void var_declaration(void){
    consume(T_IDENT,"expect variable name");
    Token name=prev; Local*l=add_local(cur_func,name.start,name.length);
    if(match(T_EQ)) expression();
    else emit("xorpd %%xmm0, %%xmm0");
    emit("movsd %%xmm0, %d(%%rbp)",l->offset);
}

static void assignment_or_expr(void){
    if(check(T_IDENT)){
        Token name=cur; Lexer save=LX; Token sc=cur, sp=prev;
        advance();
        if(check(T_LBRACKET)){
            /* array store: a[i] = expr */
            advance();
            Local*l=find_local(cur_func,name.start,name.length);
            if(!l){ error("undefined array"); return; }
            emit("movsd %d(%%rbp), %%xmm0",l->offset);
            emit("movq %%xmm0, %%r11"); emit("push %%r11");   /* base */
            expression();                                     /* index */
            consume(T_RBRACKET,"expect ']'");
            emit("cvttsd2si %%xmm0, %%rax"); emit("push %%rax"); /* index */
            consume(T_EQ,"expect '=' in array assignment");
            expression();                                     /* value in xmm0 */
            emit("pop %%rax"); emit("pop %%r11");
            emit("movsd %%xmm0, (%%r11,%%rax,8)");
            return;
        }
        if(check(T_EQ)){
            advance();
            Local*l=find_local(cur_func,name.start,name.length);
            if(!l){ error("assignment to undefined variable"); return; }
            expression();
            emit("movsd %%xmm0, %d(%%rbp)",l->offset);
            return;
        }
        LX=save; cur=sc; prev=sp;
    }
    expression();
}

/* if_body: compiles one if/elseif/else chain sharing a single end label.
   Never consumes the closing 'end' — the top-level if_statement does that
   once, after the entire chain, so every branch jumps to the same end. */
static void if_chain(int end_label){
    expression();
    int els=next_label();
    emit("xorpd %%xmm1, %%xmm1");
    emit("ucomisd %%xmm1, %%xmm0");
    emit("je .L%d",els);              /* false -> next branch */
    while(!check(T_ELSE)&&!check(T_END)&&!check(T_EOF)) statement();
    emit("jmp .L%d",end_label);       /* done -> skip the rest */
    emit_label(".L%d",els);
    if(match(T_ELSE)){
        if(match(T_IF)){
            if_chain(end_label);      /* else if: another test, same end */
            return;
        }
        while(!check(T_END)&&!check(T_EOF)) statement();  /* plain else */
    }
}

static void if_statement(void){
    int end=next_label();
    if_chain(end);
    emit_label(".L%d",end);
    consume(T_END,"expect 'end' after if"); match(T_IF);
}

static void while_statement(void){
    int top=next_label(), end=next_label();
    emit_label(".L%d",top);
    expression();
    emit("xorpd %%xmm1, %%xmm1"); emit("ucomisd %%xmm1, %%xmm0"); emit("je .L%d",end);
    while(!check(T_END)&&!check(T_EOF)) statement();
    emit("jmp .L%d",top);
    emit_label(".L%d",end);
    consume(T_END,"expect 'end' after while"); match(T_WHILE);
}

static void for_statement(void){
    consume(T_IDENT,"expect loop variable");
    Token var=prev; Local*iv=add_local(cur_func,var.start,var.length);
    consume(T_EQ,"expect '='"); expression();
    emit("movsd %%xmm0, %d(%%rbp)",iv->offset);
    consume(T_TO,"expect 'to'");
    char hid[64]; snprintf(hid,sizeof(hid)," lim%d",next_label());
    Local*lim=add_local(cur_func,hid,(int)strlen(hid));
    expression(); emit("movsd %%xmm0, %d(%%rbp)",lim->offset);
    int top=next_label(), end=next_label();
    emit_label(".L%d",top);
    emit("movsd %d(%%rbp), %%xmm0",iv->offset);
    emit("movsd %d(%%rbp), %%xmm1",lim->offset);
    emit("ucomisd %%xmm1, %%xmm0"); emit("ja .L%d",end);   /* i > lim -> stop */
    while(!check(T_END)&&!check(T_EOF)) statement();
    emit("movsd %d(%%rbp), %%xmm0",iv->offset);
    emit("movsd .Lone(%%rip), %%xmm1"); emit("addsd %%xmm1, %%xmm0");
    emit("movsd %%xmm0, %d(%%rbp)",iv->offset);
    emit("jmp .L%d",top);
    emit_label(".L%d",end);
    consume(T_END,"expect 'end' after for"); match(T_FOR);
}

static void return_statement(void){
    if(check(T_END)||check(T_EOF)) emit("xorpd %%xmm0, %%xmm0");
    else expression();
    emit("jmp .Lret_%s",cur_func->name);
}
static void asm_statement(void){
    consume(T_STRING,"expect string after asm");
    str_add(&TEXT,"    %.*s\n",prev.length,prev.start);
}
static void statement(void){
    if(match(T_PRINT)) print_statement();
    else if(match(T_VAR)) var_declaration();
    else if(match(T_IF)) if_statement();
    else if(match(T_WHILE)) while_statement();
    else if(match(T_FOR)) for_statement();
    else if(match(T_RETURN)) return_statement();
    else if(match(T_ASM)) asm_statement();
    else assignment_or_expr();
}

/* ======================================================================== */
/*  Functions                                                                */
/* ======================================================================== */

static void compile_function(void){
    consume(T_IDENT,"expect function name");
    Token name=prev; Func*f=&funcs[func_count++];
    int n=name.length<63?name.length:63; memcpy(f->name,name.start,n); f->name[n]='\0';
    f->arity=0; f->local_count=0; f->frame_size=0; cur_func=f;

    consume(T_LPAREN,"expect '('");
    if(!check(T_RPAREN)){
        do{ consume(T_IDENT,"expect parameter"); add_local(f,prev.start,prev.length); f->arity++; }while(match(T_COMMA));
    }
    consume(T_RPAREN,"expect ')'");

    Str real=TEXT; Str body; str_init(&body); TEXT=body;
    for(int i=0;i<f->arity;i++) emit("movsd %s, %d(%%rbp)",ARG_XMM[i],f->locals[i].offset);
    while(!check(T_END)&&!check(T_EOF)) statement();
    consume(T_END,"expect 'end'"); match(T_FUNC);
    emit("xorpd %%xmm0, %%xmm0");
    emit_label(".Lret_%s",f->name);
    body=TEXT; TEXT=real;

    int frame=(f->frame_size+15)&~15;
    const char*sym=(strcmp(f->name,"main")==0)?"mike_main":f->name;
    str_add(&TEXT,"\n.globl %s\n%s:\n",sym,sym);
    emit("push %%rbp"); emit("mov %%rsp, %%rbp");
    if(frame>0) emit("sub $%d, %%rsp",frame);
    str_add(&TEXT,"%s",body.buf);
    emit("mov %%rbp, %%rsp"); emit("pop %%rbp"); emit("ret");
    free(body.buf);
}

static void parse_prelude_functions(void){
    if(g_prelude.len==0) return;
    /* save lexer/token state, lex the prelude as standalone functions */
    Lexer save_lx=LX; Token save_cur=cur, save_prev=prev;
    lexer_init(g_prelude.buf);
    advance();
    while(!check(T_EOF)){
        if(match(T_FUNC)) compile_function();
        else if(check(T_EOF)) break;
        else advance();  /* skip stray tokens between funcs */
    }
    /* restore main source state */
    LX=save_lx; cur=save_cur; prev=save_prev;
}

static void parse_module(void){
    consume(T_START,"expect 'start'"); consume(T_MODULE,"expect 'module'");
    match(T_SEMI); if(check(T_IDENT)) advance();
    /* consume any number of 'lib <name>' or 'lib a/b' declarations */
    while(match(T_LIB)){ advance(); if(match(T_SLASH)) advance(); }
    /* inject library functions before user code so calls resolve */
    parse_prelude_functions();
    while(!check(T_END)&&!check(T_EOF)){
        if(match(T_FUNC)) compile_function();
        else { error("only functions at module level"); advance(); }
    }
    consume(T_END,"expect 'end modul'"); consume(T_MODUL,"expect 'modul'");
}

/* ======================================================================== */
/*  Runtime prelude (assembly). Uses libc: malloc, printf-free number output */
/*  is hand-rolled to keep output format clean.                              */
/* ======================================================================== */

static const char*RUNTIME =
"# ---- runtime ----\n"
".globl main\n"                       /* real C main -> lets libc/libm init */
"main:\n"
"    push %rbp\n"
"    mov %rsp, %rbp\n"
"    call mike_main\n"
"    xor %eax, %eax\n"                  /* return 0 from C main */
"    pop %rbp\n"
"    ret\n"
"\n"
"print_newline:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $16, %rsp\n"
"    movb $10, -1(%rbp)\n"
"    mov $1, %rdi\n    lea -1(%rbp), %rsi\n    mov $1, %rdx\n    call write\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# put_char_rt(code in xmm0): write one byte (ASCII code) to stdout\n"
"put_char_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $16, %rsp\n"
"    cvttsd2si %xmm0, %rax\n"
"    mov %al, -1(%rbp)\n"
"    mov $1, %rdi\n    lea -1(%rbp), %rsi\n    mov $1, %rdx\n    call write\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# put_str_rt(ptr in %rdi, len in %rsi): write string to stdout, no newline\n"
"put_str_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    mov %rsi, %rdx\n"            /* len */
"    mov %rdi, %rsi\n"           /* buffer */
"    mov $1, %rdi\n    call write\n"
"    pop %rbp\n    ret\n"
"\n"
"# trailing zeros trimmed. Handled with integer math on scaled value.\n"
"print_double:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $64, %rsp\n"
"    push %r12\n    push %r13\n    push %r14\n"
"    # handle sign\n"
"    xorpd %xmm1, %xmm1\n"
"    ucomisd %xmm0, %xmm1\n"
"    jbe .Lpd_pos\n"                    /* 0 <= x -> positive */
"    # negative: print '-', negate\n"
"    movb $45, -1(%rbp)\n"
"    mov $1, %rdi\n    lea -1(%rbp), %rsi\n    mov $1, %rdx\n    call write\n"
"    movsd .Lsignmask(%rip), %xmm2\n    xorpd %xmm2, %xmm0\n"
".Lpd_pos:\n"
"    # integer part = trunc(x)\n"
"    cvttsd2si %xmm0, %r12\n"           /* r12 = int part */
"    cvtsi2sd %r12, %xmm3\n"
"    subsd %xmm3, %xmm0\n"              /* xmm0 = fractional */
"    # frac scaled to 1e6, rounded\n"
"    movsd .Lmillion(%rip), %xmm4\n"
"    mulsd %xmm4, %xmm0\n"
"    movsd .Lhalf(%rip), %xmm5\n    addsd %xmm5, %xmm0\n"  /* round */
"    cvttsd2si %xmm0, %r13\n"           /* r13 = frac (0..1e6) */
"    cmp $1000000, %r13\n"
"    jl .Lpd_int\n"
"    sub $1000000, %r13\n    inc %r12\n"  /* carry */
".Lpd_int:\n"
"    # print integer part (r12) via print_uint\n"
"    mov %r12, %rdi\n    call print_uint\n"
"    # if frac==0, done\n"
"    cmp $0, %r13\n    je .Lpd_end\n"
"    # print '.'\n"
"    movb $46, -1(%rbp)\n"
"    mov $1, %rdi\n    lea -1(%rbp), %rsi\n    mov $1, %rdx\n    call write\n"
"    # print 6 fractional digits (r13, 0..999999) then trim trailing zeros\n"
"    # write digits into buffer at -20(%rbp)..-15(%rbp)\n"
"    mov %r13, %rax\n"
"    lea -14(%rbp), %rsi\n"           /* one past the 6-digit field */
"    mov $6, %r14\n"                  /* counter */
".Lpd_fdig:\n"
"    xor %rdx, %rdx\n"
"    mov $10, %rcx\n"
"    div %rcx\n"                      /* rax=rax/10, rdx=rem */
"    add $48, %rdx\n"
"    dec %rsi\n"
"    mov %dl, (%rsi)\n"
"    dec %r14\n"
"    jnz .Lpd_fdig\n"
"    # now rsi -> first digit (at -20). find trimmed length in r8\n"
"    mov $6, %r8\n"
"    lea -14(%rbp), %rdi\n"           /* points just past last digit */
".Lpd_trim:\n"
"    dec %rdi\n"
"    cmpb $48, (%rdi)\n"
"    jne .Lpd_wf\n"
"    dec %r8\n"
"    cmp $0, %r8\n"
"    jg .Lpd_trim\n"
".Lpd_wf:\n"
"    mov $1, %rdi\n    lea -20(%rbp), %rsi\n    mov %r8, %rdx\n    call write\n"
".Lpd_end:\n"
"    pop %r14\n    pop %r13\n    pop %r12\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# print_uint: unsigned integer in %rdi -> stdout, no newline\n"
"print_uint:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $32, %rsp\n"
"    mov %rdi, %rax\n    lea -1(%rbp), %rsi\n    mov $0, %r8\n"
".Lpu_l:\n"
"    xor %rdx, %rdx\n    mov $10, %rcx\n    div %rcx\n"
"    add $48, %rdx\n    mov %dl, (%rsi)\n    dec %rsi\n    inc %r8\n"
"    cmp $0, %rax\n    jne .Lpu_l\n"
"    inc %rsi\n    mov $1, %rdi\n    mov %r8, %rdx\n    call write\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# read_double: read chars until newline/EOF into a buffer, parse with strtod\n"
"read_double:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $64, %rsp\n"
"    push %rbx\n    push %r12\n"           /* callee-saved; 2 pushes keep alignment */
"    lea -56(%rbp), %rbx\n"           /* rbx = buffer write ptr (survives syscall) */
"    xor %r12, %r12\n"                /* count, cap at 47 */
".Lrd_loop:\n"
"    cmp $47, %r12\n    jge .Lrd_term\n"
"    mov $0, %rdi\n    lea -60(%rbp), %rsi\n    mov $1, %rdx\n    call read\n"
"    cmp $0, %rax\n    jle .Lrd_term\n"       /* EOF */
"    movzbq -60(%rbp), %rcx\n"
"    cmp $10, %rcx\n    je .Lrd_term\n"       /* newline stops */
"    mov %cl, (%rbx)\n    inc %rbx\n    inc %r12\n"
"    jmp .Lrd_loop\n"
".Lrd_term:\n"
"    movb $0, (%rbx)\n"                       /* NUL-terminate */
"    cmp $0, %r12\n    jne .Lrd_parse\n"
"    xorpd %xmm0, %xmm0\n    jmp .Lrd_end\n"   /* empty -> 0.0 */
".Lrd_parse:\n"
"    lea -56(%rbp), %rdi\n    xor %rsi, %rsi\n    call strtod\n"  /* xmm0 = value */
".Lrd_end:\n"
"    pop %r12\n    pop %rbx\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# ---- RNG: xorshift64* -> uniform [0,1); range; gaussian (Box-Muller) ----\n"
"rng_seed:\n"                             /* seed in rdi */
"    mov %rdi, rng_state(%rip)\n"
"    cmp $0, %rdi\n    jne .Lrs_ok\n    movq $88172645463325252, %rax\n    mov %rax, rng_state(%rip)\n"
".Lrs_ok:\n    ret\n"
"\n"
"rng_u64:\n"                              /* -> rax : next 64-bit */
"    mov rng_state(%rip), %rax\n"
"    mov %rax, %rcx\n    shr $12, %rcx\n    xor %rcx, %rax\n"
"    mov %rax, %rcx\n    shl $25, %rcx\n    xor %rcx, %rax\n"
"    mov %rax, %rcx\n    shr $27, %rcx\n    xor %rcx, %rax\n"
"    mov %rax, rng_state(%rip)\n"
"    movabs $0x2545F4914F6CDD1D, %rcx\n    imul %rcx, %rax\n"
"    ret\n"
"\n"
"rng_next:\n"                             /* -> xmm0 in [0,1) */
"    push %rbp\n    mov %rsp, %rbp\n"
"    call rng_u64\n"
"    shr $11, %rax\n"                     /* 53 bits */
"    cvtsi2sd %rax, %xmm0\n"
"    movsd .Ltwo53(%rip), %xmm1\n    divsd %xmm1, %xmm0\n"
"    pop %rbp\n    ret\n"
"\n"
"rng_range:\n"                            /* lo=xmm0, hi=xmm1 -> xmm0 */
"    push %rbp\n    mov %rsp, %rbp\n    sub $16, %rsp\n"
"    movsd %xmm0, -8(%rbp)\n    movsd %xmm1, -16(%rbp)\n"
"    call rng_next\n"                     /* u in xmm0 */
"    movsd -16(%rbp), %xmm1\n    movsd -8(%rbp), %xmm2\n"
"    subsd %xmm2, %xmm1\n"                /* hi-lo */
"    mulsd %xmm1, %xmm0\n    addsd %xmm2, %xmm0\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"rng_gauss:\n"                            /* mu=xmm0, sd=xmm1 -> xmm0 */
"    push %rbp\n    mov %rsp, %rbp\n    sub $32, %rsp\n"
"    movsd %xmm0, -8(%rbp)\n    movsd %xmm1, -16(%rbp)\n"
"    call rng_next\n    movsd %xmm0, -24(%rbp)\n"   /* u1 */
".Lrg_u1:\n"
"    movsd -24(%rbp), %xmm0\n    xorpd %xmm1, %xmm1\n    ucomisd %xmm1, %xmm0\n    jne .Lrg_ok\n"
"    call rng_next\n    movsd %xmm0, -24(%rbp)\n    jmp .Lrg_u1\n"  /* avoid log(0) */
".Lrg_ok:\n"
"    call rng_next\n    movsd %xmm0, -32(%rbp)\n"   /* u2 */
"    # z = sqrt(-2 ln u1) * cos(2pi u2)\n"
"    movsd -24(%rbp), %xmm0\n    call log\n"
"    movsd .Lnegtwo(%rip), %xmm1\n    mulsd %xmm1, %xmm0\n    call sqrt\n"
"    movsd %xmm0, -24(%rbp)\n"                       /* radius */
"    movsd -32(%rbp), %xmm0\n    movsd .Ltwopi(%rip), %xmm1\n    mulsd %xmm1, %xmm0\n    call cos\n"
"    mulsd -24(%rbp), %xmm0\n"                       /* z */
"    movsd -16(%rbp), %xmm1\n    mulsd %xmm1, %xmm0\n"  /* * sd */
"    movsd -8(%rbp), %xmm1\n    addsd %xmm1, %xmm0\n"   /* + mu */
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# ---- CSV: read whitespace/comma-separated numbers via libc ----\n"
"# csv_count(path in %rdi) -> xmm0 = count of numbers\n"
"csv_count:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    push %r12\n    push %r13\n"          /* 2 pushes: 16-byte aligned */
"    sub $16, %rsp\n"
"    lea .Lcsv_mode(%rip), %rsi\n    call fopen\n"
"    mov %rax, %r12\n"                    /* FILE* */
"    xorpd %xmm0, %xmm0\n"
"    cmp $0, %r12\n    je .Lcc_done\n"
"    xor %r13, %r13\n"                    /* count */
".Lcc_loop:\n"
"    mov %r12, %rdi\n    lea .Lcsv_fmt(%rip), %rsi\n    lea -8(%rbp), %rdx\n    xor %eax, %eax\n    call fscanf\n"
"    cmp $1, %eax\n    jne .Lcc_end\n"
"    inc %r13\n    jmp .Lcc_loop\n"
".Lcc_end:\n"
"    mov %r12, %rdi\n    call fclose\n"
"    cvtsi2sd %r13, %xmm0\n"
".Lcc_done:\n"
"    add $16, %rsp\n    pop %r13\n    pop %r12\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# csv_load_rt(path in %rdi, array in %rsi) -> xmm0 = count\n"
"csv_load_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    push %r12\n    push %r13\n    push %r14\n    push %r15\n"   /* 4 pushes: keeps 16-byte alignment */
"    sub $16, %rsp\n"
"    mov %rsi, %r14\n"                    /* array base */
"    lea .Lcsv_mode(%rip), %rsi\n    call fopen\n"
"    mov %rax, %r12\n"
"    xorpd %xmm0, %xmm0\n"
"    cmp $0, %r12\n    je .Lcl_done\n"
"    xor %r13, %r13\n"                    /* index */
".Lcl_loop:\n"
"    mov %r12, %rdi\n    lea .Lcsv_fmt(%rip), %rsi\n    lea -8(%rbp), %rdx\n    xor %eax, %eax\n    call fscanf\n"
"    cmp $1, %eax\n    jne .Lcl_end\n"
"    movsd -8(%rbp), %xmm1\n"
"    movsd %xmm1, (%r14,%r13,8)\n"        /* array[i] = value */
"    inc %r13\n    jmp .Lcl_loop\n"
".Lcl_end:\n"
"    mov %r12, %rdi\n    call fclose\n"
"    cvtsi2sd %r13, %xmm0\n"
".Lcl_done:\n"
"    add $16, %rsp\n"
"    pop %r15\n    pop %r14\n    pop %r13\n    pop %r12\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n"
"\n"
"# file_open_rt(path in %rdi): fopen(path,\"w\") -> stored in out_file\n"
"file_open_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    lea .Lwmode(%rip), %rsi\n    call fopen\n"
"    mov %rax, out_file(%rip)\n"
"    pop %rbp\n    ret\n"
"\n"
"# file_str_rt(ptr in %rdi, len in %rsi): fwrite(ptr,1,len,out_file)\n"
"file_str_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    mov out_file(%rip), %rcx\n"    /* stream */
"    cmp $0, %rcx\n    je .Lfs_done\n"
"    mov %rsi, %rdx\n"             /* nmemb = len */
"    mov $1, %rsi\n"              /* size = 1 */
"    call fwrite\n"               /* rdi=ptr already, rsi=1, rdx=len, rcx=stream */
".Lfs_done:\n"
"    pop %rbp\n    ret\n"
"\n"
"# file_num_rt(value in %xmm0): fprintf(out_file, \"%g\", value)\n"
"file_num_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    mov out_file(%rip), %rdi\n"
"    cmp $0, %rdi\n    je .Lfn_done\n"
"    lea .Lgfmt(%rip), %rsi\n"
"    mov $1, %eax\n"           /* 1 vector reg used */
"    call fprintf\n"
".Lfn_done:\n"
"    pop %rbp\n    ret\n"
"\n"
"# file_close_rt(): fclose(out_file)\n"
"file_close_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n"
"    mov out_file(%rip), %rdi\n"
"    cmp $0, %rdi\n    je .Lfc_done\n"
"    call fclose\n"
"    movq $0, out_file(%rip)\n"
".Lfc_done:\n"
"    pop %rbp\n    ret\n"
"\n"
"# putc_rt(code in %rdi): write one byte to stdout, no newline\n"
"putc_rt:\n"
"    push %rbp\n    mov %rsp, %rbp\n    sub $16, %rsp\n"
"    mov %dil, -1(%rbp)\n"          /* store low byte */
"    mov $1, %rdi\n    lea -1(%rbp), %rsi\n    mov $1, %rdx\n    call write\n"
"    mov %rbp, %rsp\n    pop %rbp\n    ret\n";

static const char*CONSTS =
".align 8\n"
".Lsignmask:\n    .quad 0x8000000000000000\n"
".Labsmask:\n    .quad 0x7fffffffffffffff\n"
".Lone:\n    .double 1.0\n"
".Lhalf:\n    .double 0.5\n"
".Lmillion:\n    .double 1000000.0\n"
".Ltwo53:\n    .double 9007199254740992.0\n"
".Lnegtwo:\n    .double -2.0\n"
".Ltwopi:\n    .double 6.283185307179586\n"
".Lcsv_mode:\n    .asciz \"r\"\n"
".Lcsv_fmt:\n    .asciz \" %lf ,\"\n"
".Lwmode:\n    .asciz \"w\"\n"
".Lgfmt:\n    .asciz \"%g\"\n"
"out_file:\n    .quad 0\n"
"rng_state:\n    .quad 0\n";

static char*read_file(const char*path){
    FILE*f=fopen(path,"rb"); if(!f){ fprintf(stderr,"mike: cannot open %s\n",path); exit(74);} 
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    char*b=malloc(sz+1); if(fread(b,1,sz,f)!=(size_t)sz){ fprintf(stderr,"read error\n"); exit(74);} 
    b[sz]='\0'; fclose(f); return b;
}

/* Splice the function bodies from a library .mik into the prelude buffer.
   A library file is just funcs wrapped in a module; we lift the inner funcs
   (from the first 'func ' up to 'end modul') and inject them into the caller. */
static void load_library(const char*name,int len){
    if(lib_already(name,len)) return;
    lib_mark(name,len);
    char nm[64]; int k=len<63?len:63; memcpy(nm,name,k); nm[k]='\0';
    /* input/output is handled natively by the compiler, no file needed */
    if(strcmp(nm,"input")==0||strcmp(nm,"output")==0) return;

    /* search order for <name>.mik:
       1. <dir-of-source>/lib   (project-local)
       2. $MIKE_LIB             (env override)
       3. ~/.mike/lib           (per-user install)
       4. /usr/local/share/mike/lib  (system install) */
    char cand[800]; FILE*tf=NULL; char path[800]={0};
    const char*dirs[4]; char envbuf[600]={0}, homebuf[600]={0};
    dirs[0]=g_libdir;
    dirs[1]=getenv("MIKE_LIB");
    const char*home=getenv("HOME");
    if(home){ snprintf(homebuf,sizeof(homebuf),"%s/.mike/lib",home); dirs[2]=homebuf; } else dirs[2]=NULL;
    dirs[3]="/usr/local/share/mike/lib";
    (void)envbuf;
    for(int i=0;i<4;i++){
        if(!dirs[i]||!dirs[i][0]) continue;
        snprintf(cand,sizeof(cand),"%s/%s.mik",dirs[i],nm);
        tf=fopen(cand,"rb");
        if(tf){ strncpy(path,cand,sizeof(path)-1); break; }
    }
    if(!tf){ fprintf(stderr,"mike: library '%s' not found (searched project lib/, $MIKE_LIB, ~/.mike/lib, /usr/local/share/mike/lib)\n",nm); return; }
    fclose(tf);
    char*src=read_file(path);
    const char*first=strstr(src,"func ");
    const char*endmod=strstr(src,"end modul");
    if(!endmod) endmod=src+strlen(src);
    if(first&&first<endmod) str_add(&g_prelude,"%.*s\n",(int)(endmod-first),first);
    free(src);
}

int main(int argc,char**argv){
    bool asm_only=false; const char*path=NULL;
    for(int i=1;i<argc;i++){ if(strcmp(argv[i],"-S")==0) asm_only=true; else path=argv[i]; }
    if(!path){ fprintf(stderr,"usage: mike [-S] program.mik\n"); return 64; }

    char*src=read_file(path);

    /* set library search dir: <dir-of-source>/lib */
    { char*pc=strdup(path); char*d=dirname(pc);
      snprintf(g_libdir,sizeof(g_libdir),"%s/lib",d); free(pc); }

    /* pre-scan for 'lib <name>' lines and load each library's functions */
    str_init(&g_prelude);
    { const char*q=src;
      while((q=strstr(q,"lib "))!=NULL){
        /* ensure 'lib' is a token (preceded by start/space/newline) */
        const char*name=q+4;
        while(*name==' '||*name=='\t') name++;
        const char*e=name;
        while(is_alpha(*e)||is_digit(*e)) e++;
        if(e>name) load_library(name,(int)(e-name));
        /* also handle input/output form: lib input/output -> skip '/output' */
        q=e;
      }
    }

    str_init(&TEXT); str_init(&DATA);
    lexer_init(src); advance(); parse_module();
    if(!check(T_EOF)) error("expected end of file");
    if(!find_func("main",4)){ fprintf(stderr,"mike: no main()\n"); return 65; }
    if(had_error) return 65;

    Str out; str_init(&out);
    str_add(&out,".text\n");
    str_add(&out,"%s\n",RUNTIME);
    str_add(&out,"%s\n",TEXT.buf);
    str_add(&out,".data\n");
    str_add(&out,"%s\n",CONSTS);
    str_add(&out,"%s\n",DATA.buf);

    char*pcopy=strdup(path); char*base=basename(pcopy);
    char stem[256]; strncpy(stem,base,sizeof(stem)-1); stem[sizeof(stem)-1]='\0';
    char*dot=strrchr(stem,'.'); if(dot)*dot='\0';

    char sfile[300]; snprintf(sfile,sizeof(sfile),"%s.s",stem);
    FILE*sf=fopen(sfile,"w"); if(!sf){ fprintf(stderr,"cannot write %s\n",sfile); return 74; }
    fputs(out.buf,sf); fclose(sf);
    if(asm_only){ printf("wrote %s\n",sfile); return 0; }

    char cmd[900];
    /* Assemble + link via the system C compiler (cc = gcc on Linux, clang on
       FreeBSD). libc/libm resolve the write/read/malloc/math calls, so the
       same assembly works on any x86-64 Unix. -no-pie keeps addressing simple;
       if the toolchain rejects it, fall back to a plain link. */
    snprintf(cmd,sizeof(cmd),"cc -no-pie -o %s %s -lm 2>/tmp/mike_link.err",stem,sfile);
    if(system(cmd)!=0){
        /* retry without -no-pie (some toolchains/targets differ) */
        snprintf(cmd,sizeof(cmd),"cc -o %s %s -lm 2>/tmp/mike_link.err",stem,sfile);
        if(system(cmd)!=0){
            fprintf(stderr,"mike: assemble/link failed:\n");
            if(system("cat /tmp/mike_link.err 1>&2")){}
            return 70;
        }
    }
    remove(sfile);
    printf("compiled %s -> ./%s\n",path,stem);
    return 0;
}
