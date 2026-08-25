/* ============================================================================
 * mike — a native compiler for the Mike language
 *
 *   mike hello.mik   ->   generates x86-64 assembly, assembles with `as`,
 *                         links with `ld`, produces a standalone ./hello
 *
 *   Pipeline:  source -> lexer -> parser -> x86-64 codegen (.s)
 *                     -> as (assemble) -> ld (link) -> native ELF
 *
 *   No C in the output path. No libc — we talk to the kernel via syscalls.
 *
 *   Features: var, assignment, integer arithmetic, comparison, if/else,
 *             while, for..to, functions with params + return, print (string
 *             and integer), and a raw `asm "..."` escape hatch.
 *
 *   Build:    cc -O2 -o mike mike.c
 *   Use:      ./mike hello.mik      # -> ./hello
 *             ./mike -S hello.mik   # -> hello.s only (keep the assembly)
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
    T_AND, T_OR, T_NOT, T_ASM, T_INPUT_INT,
    T_LPAREN, T_RPAREN, T_COMMA, T_SEMI, T_SLASH,
    T_PLUS, T_MINUS, T_STAR, T_PERCENT,
    T_EQ, T_EQEQ, T_BANGEQ, T_LT, T_LTE, T_GT, T_GTE,
    T_EOF, T_ERROR
} TokType;

typedef struct {
    TokType type;
    const char *start;
    int length;
    int line;
    long number;
} Token;

typedef struct { const char *cur; int line; } Lexer;

static Lexer LX;
static void lexer_init(const char *src){ LX.cur=src; LX.line=1; }

static bool is_alpha(char c){ return isalpha((unsigned char)c)||c=='_'; }
static bool is_digit(char c){ return isdigit((unsigned char)c); }

static Token mk(TokType t, const char *s, int len){
    Token k; k.type=t; k.start=s; k.length=len; k.line=LX.line; k.number=0; return k;
}
static Token err_tok(const char *m){ Token k=mk(T_ERROR,m,(int)strlen(m)); return k; }

static void skip_ws(void){
    for(;;){
        char c=*LX.cur;
        if(c==' '||c=='\t'||c=='\r') LX.cur++;
        else if(c=='\n'){ LX.line++; LX.cur++; }
        else if(c=='#'){ while(*LX.cur && *LX.cur!='\n') LX.cur++; }
        else return;
    }
}
static TokType kw(const char *s,int n){
    struct{const char*k;TokType t;}K[]={
        {"start",T_START},{"module",T_MODULE},{"modul",T_MODUL},{"lib",T_LIB},
        {"print",T_PRINT},{"var",T_VAR},{"func",T_FUNC},{"return",T_RETURN},
        {"end",T_END},{"if",T_IF},{"else",T_ELSE},{"while",T_WHILE},
        {"for",T_FOR},{"to",T_TO},{"and",T_AND},{"or",T_OR},{"not",T_NOT},
        {"asm",T_ASM},{"input_int",T_INPUT_INT},
    };
    for(unsigned i=0;i<sizeof(K)/sizeof(K[0]);i++)
        if((int)strlen(K[i].k)==n && memcmp(K[i].k,s,n)==0) return K[i].t;
    return T_IDENT;
}
static Token lex_next(void){
    skip_ws();
    const char *s=LX.cur; char c=*LX.cur;
    if(c=='\0') return mk(T_EOF,s,0);
    LX.cur++;
    if(is_alpha(c)){
        while(is_alpha(*LX.cur)||is_digit(*LX.cur)) LX.cur++;
        int len=(int)(LX.cur-s);
        Token t=mk(kw(s,len),s,len);
        return t;
    }
    if(is_digit(c)){
        while(is_digit(*LX.cur)) LX.cur++;
        Token t=mk(T_NUMBER,s,(int)(LX.cur-s));
        t.number=strtol(s,NULL,10);
        return t;
    }
    if(c=='"'){
        const char *b=LX.cur;
        while(*LX.cur && *LX.cur!='"'){ if(*LX.cur=='\n')LX.line++; LX.cur++; }
        if(*LX.cur=='\0') return err_tok("unterminated string");
        Token t=mk(T_STRING,b,(int)(LX.cur-b));
        LX.cur++;
        return t;
    }
    switch(c){
        case '(':return mk(T_LPAREN,s,1);
        case ')':return mk(T_RPAREN,s,1);
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
/*  Output buffer for generated assembly                                     */
/* ======================================================================== */

typedef struct { char *buf; size_t len, cap; } Str;
static void str_init(Str *s){ s->cap=4096; s->len=0; s->buf=malloc(s->cap); s->buf[0]='\0'; }
static void str_add(Str *s, const char *fmt, ...){
    va_list a; va_start(a,fmt);
    va_list a2; va_copy(a2,a);
    int n=vsnprintf(NULL,0,fmt,a);      /* measure required length */
    va_end(a);
    if(n<0){ va_end(a2); return; }
    if(s->len+n+1>s->cap){ while(s->len+n+1>s->cap) s->cap*=2; s->buf=realloc(s->buf,s->cap); }
    vsnprintf(s->buf+s->len,n+1,fmt,a2);
    va_end(a2);
    s->len+=n;
}

/* Two sections: .text (code) and .data (string literals) */
static Str TEXT, DATA;

/* ======================================================================== */
/*  Symbol tables                                                            */
/* ======================================================================== */

typedef struct { char name[64]; int offset; } Local;   /* offset from %rbp */
typedef struct {
    char name[64];
    int  arity;
    Local locals[128];
    int  local_count;
    int  frame_size;   /* bytes reserved on stack */
} Func;

static Func funcs[128];
static int  func_count;
static Func *cur_func;

static int str_literal_count;
static int label_count;
static int next_label(void){ return label_count++; }

static Func *find_func(const char *name,int len){
    for(int i=0;i<func_count;i++)
        if((int)strlen(funcs[i].name)==len && memcmp(funcs[i].name,name,len)==0)
            return &funcs[i];
    return NULL;
}
static Local *find_local(Func *f,const char *name,int len){
    for(int i=0;i<f->local_count;i++)
        if((int)strlen(f->locals[i].name)==len && memcmp(f->locals[i].name,name,len)==0)
            return &f->locals[i];
    return NULL;
}
static Local *add_local(Func *f,const char *name,int len){
    Local *l=find_local(f,name,len);
    if(l) return l;
    l=&f->locals[f->local_count++];
    int n=len<63?len:63; memcpy(l->name,name,n); l->name[n]='\0';
    f->frame_size += 8;
    l->offset = -f->frame_size;   /* grows downward from rbp */
    return l;
}

/* ======================================================================== */
/*  Parser + codegen  (single pass, stack-machine style)                     */
/*                                                                           */
/*  Convention: every expression leaves its result in %rax.                  */
/*  Binary ops: eval left -> push; eval right -> pop left into %rcx; combine. */
/* ======================================================================== */

static Token cur, prev;
static bool had_error;

static void error(const char *msg){
    fprintf(stderr,"[line %d] error at '%.*s': %s\n",
            cur.line, cur.length, cur.start, msg);
    had_error=true;
}
static void advance(void){
    prev=cur;
    for(;;){ cur=lex_next(); if(cur.type!=T_ERROR) break; error(cur.start); }
}
static bool check(TokType t){ return cur.type==t; }
static bool match(TokType t){ if(!check(t))return false; advance(); return true; }
static void consume(TokType t,const char*m){ if(check(t)){advance();return;} error(m); }

static void expression(void);
static void statement(void);

/* --- emit helpers --- */
static void emit(const char *fmt,...){
    va_list a; va_start(a,fmt);
    char tmp[512]; vsnprintf(tmp,sizeof(tmp),fmt,a); va_end(a);
    str_add(&TEXT,"    %s\n",tmp);
}
static void emit_label(const char *fmt,...){
    va_list a; va_start(a,fmt);
    char tmp[128]; vsnprintf(tmp,sizeof(tmp),fmt,a); va_end(a);
    str_add(&TEXT,"%s:\n",tmp);
}

/* add a NUL-terminated string literal to .data, return its label id */
static int add_string(const char *s,int len){
    int id=str_literal_count++;
    str_add(&DATA,"str_%d:\n    .ascii \"",id);
    for(int i=0;i<len;i++){
        char c=s[i];
        if(c=='\\'&&i+1<len){ /* pass simple escapes through */
            char n=s[i+1];
            if(n=='n'){ str_add(&DATA,"\\n"); i++; continue; }
            if(n=='t'){ str_add(&DATA,"\\t"); i++; continue; }
            if(n=='"'){ str_add(&DATA,"\\\""); i++; continue; }
            if(n=='\\'){ str_add(&DATA,"\\\\"); i++; continue; }
        }
        if(c=='"') str_add(&DATA,"\\\"");
        else if(c=='\n') str_add(&DATA,"\\n");
        else str_add(&DATA,"%c",c);
    }
    str_add(&DATA,"\\0\"\n");
    /* also store length as a symbol for write() */
    str_add(&DATA,"    .set str_%d_len, . - str_%d - 1\n",id,id);
    return id;
}

/* --- primary / call --- */
static void call_args_and_call(Func *f);

static void primary(void){
    if(match(T_NUMBER)){
        emit("mov $%ld, %%rax", prev.number);
        return;
    }
    if(match(T_LPAREN)){
        expression();
        consume(T_RPAREN,"expect ')'");
        return;
    }
    if(match(T_MINUS)){
        primary();
        emit("neg %%rax");
        return;
    }
    if(match(T_NOT)){
        primary();
        emit("cmp $0, %%rax");
        emit("sete %%al");
        emit("movzbq %%al, %%rax");
        return;
    }
    if(match(T_INPUT_INT)){
        /* input_int "prompt"  -> prints prompt, reads a line, parses int into %rax */
        if(check(T_STRING)){
            advance();
            int id=add_string(prev.start,prev.length);
            emit("mov $1, %%rax");
            emit("mov $1, %%rdi");
            emit("lea str_%d(%%rip), %%rsi", id);
            emit("mov $str_%d_len, %%rdx", id);
            emit("syscall");
        }
        emit("call read_int");   /* result in %rax */
        return;
    }
    if(match(T_IDENT)){
        Token name=prev;
        if(check(T_LPAREN)){
            Func *f=find_func(name.start,name.length);
            if(!f){ error("call to undefined function"); return; }
            advance(); /* '(' */
            call_args_and_call(f);
            return;
        }
        Local *l=find_local(cur_func,name.start,name.length);
        if(!l){ error("undefined variable"); return; }
        emit("mov %d(%%rbp), %%rax", l->offset);
        return;
    }
    error("expect expression");
}

/* --- binary operators with precedence climbing --- */
static int prec_of(TokType t){
    switch(t){
        case T_OR: return 1;
        case T_AND: return 2;
        case T_EQEQ: case T_BANGEQ: return 3;
        case T_LT: case T_LTE: case T_GT: case T_GTE: return 4;
        case T_PLUS: case T_MINUS: return 5;
        case T_STAR: case T_SLASH: case T_PERCENT: return 6;
        default: return 0;
    }
}
static void binary_rhs(int min_prec);

static void unary_or_primary(void){ primary(); }

static void binary_rhs(int min_prec){
    for(;;){
        int p=prec_of(cur.type);
        if(p<min_prec || p==0) return;
        TokType op=cur.type;
        advance();
        emit("push %%rax");            /* save left operand */
        unary_or_primary();
        /* handle higher-precedence tail on the right */
        for(;;){
            int p2=prec_of(cur.type);
            if(p2>p){ binary_rhs(p2); } else break;
        }
        emit("mov %%rax, %%rcx");      /* rcx = right */
        emit("pop %%rax");             /* rax = left  */
        switch(op){
            case T_PLUS:  emit("add %%rcx, %%rax"); break;
            case T_MINUS: emit("sub %%rcx, %%rax"); break;
            case T_STAR:  emit("imul %%rcx, %%rax"); break;
            case T_SLASH: emit("cqo"); emit("idiv %%rcx"); break;      /* rax=quotient */
            case T_PERCENT: emit("cqo"); emit("idiv %%rcx"); emit("mov %%rdx, %%rax"); break;
            case T_EQEQ:  emit("cmp %%rcx, %%rax"); emit("sete %%al"); emit("movzbq %%al, %%rax"); break;
            case T_BANGEQ:emit("cmp %%rcx, %%rax"); emit("setne %%al");emit("movzbq %%al, %%rax"); break;
            case T_LT:    emit("cmp %%rcx, %%rax"); emit("setl %%al"); emit("movzbq %%al, %%rax"); break;
            case T_LTE:   emit("cmp %%rcx, %%rax"); emit("setle %%al");emit("movzbq %%al, %%rax"); break;
            case T_GT:    emit("cmp %%rcx, %%rax"); emit("setg %%al"); emit("movzbq %%al, %%rax"); break;
            case T_GTE:   emit("cmp %%rcx, %%rax"); emit("setge %%al");emit("movzbq %%al, %%rax"); break;
            case T_AND:   emit("cmp $0, %%rax"); emit("setne %%al");
                          emit("cmp $0, %%rcx"); emit("setne %%cl");
                          emit("and %%cl, %%al"); emit("movzbq %%al, %%rax"); break;
            case T_OR:    emit("cmp $0, %%rax"); emit("setne %%al");
                          emit("cmp $0, %%rcx"); emit("setne %%cl");
                          emit("or %%cl, %%al"); emit("movzbq %%al, %%rax"); break;
            default: break;
        }
    }
}
static void expression(void){
    unary_or_primary();
    binary_rhs(1);
}

/* System V AMD64: first 6 int args in rdi,rsi,rdx,rcx,r8,r9 */
static const char *ARG_REGS[6]={"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};

static void call_args_and_call(Func *f){
    /* evaluate args, push each, then pop into arg registers (left to right) */
    int argc=0;
    if(!check(T_RPAREN)){
        do{
            expression();
            emit("push %%rax");
            argc++;
        }while(match(T_COMMA));
    }
    consume(T_RPAREN,"expect ')' after arguments");
    if(argc!=f->arity) error("wrong number of arguments");
    /* args were pushed left..right, so pop in reverse to fill registers */
    for(int i=argc-1;i>=0;i--) emit("pop %s", ARG_REGS[i]);
    emit("call %s", f->name);
    /* result in rax */
}

/* ======================================================================== */
/*  print — decides string vs integer at compile time                       */
/* ======================================================================== */

static void gen_print_int(void); /* helper routine label emitted once */

static void print_statement(void){
    if(check(T_STRING)){
        advance();
        int id=add_string(prev.start,prev.length);
        /* write(1, str, len) + newline */
        emit("mov $1, %%rax");            /* sys_write */
        emit("mov $1, %%rdi");            /* fd = stdout */
        emit("lea str_%d(%%rip), %%rsi", id);
        emit("mov $str_%d_len, %%rdx", id);
        emit("syscall");
        emit("call print_newline");
    } else {
        /* integer expression -> call runtime print_int */
        expression();
        emit("mov %%rax, %%rdi");
        emit("call print_int");
        emit("call print_newline");
    }
}

/* ======================================================================== */
/*  Statements                                                               */
/* ======================================================================== */

static void var_declaration(void){
    consume(T_IDENT,"expect variable name");
    Token name=prev;
    Local *l=add_local(cur_func,name.start,name.length);
    if(match(T_EQ)){
        expression();
    } else {
        emit("mov $0, %%rax");
    }
    emit("mov %%rax, %d(%%rbp)", l->offset);
}

static void assignment_or_expr_statement(void){
    if(check(T_IDENT)){
        /* peek: is it "ident =" ? */
        Token name=cur;
        Lexer save=LX; Token savecur=cur, saveprev=prev;
        advance();
        if(check(T_EQ)){
            advance();
            Local *l=find_local(cur_func,name.start,name.length);
            if(!l){ error("assignment to undefined variable"); return; }
            expression();
            emit("mov %%rax, %d(%%rbp)", l->offset);
            return;
        }
        /* not an assignment: restore and parse as expression */
        LX=save; cur=savecur; prev=saveprev;
    }
    expression();
}

static void if_statement(void){
    expression();
    int else_l=next_label(), end_l=next_label();
    emit("cmp $0, %%rax");
    emit("je .L%d", else_l);
    while(!check(T_ELSE)&&!check(T_END)&&!check(T_EOF)) statement();
    emit("jmp .L%d", end_l);
    emit_label(".L%d", else_l);
    if(match(T_ELSE)){
        while(!check(T_END)&&!check(T_EOF)) statement();
    }
    emit_label(".L%d", end_l);
    consume(T_END,"expect 'end' after if");
    match(T_IF);
}

static void while_statement(void){
    int top=next_label(), end=next_label();
    emit_label(".L%d", top);
    expression();
    emit("cmp $0, %%rax");
    emit("je .L%d", end);
    while(!check(T_END)&&!check(T_EOF)) statement();
    emit("jmp .L%d", top);
    emit_label(".L%d", end);
    consume(T_END,"expect 'end' after while");
    match(T_WHILE);
}

static void for_statement(void){
    /* for i = start to limit ... end for   (inclusive) */
    consume(T_IDENT,"expect loop variable");
    Token var=prev;
    Local *iv=add_local(cur_func,var.start,var.length);
    consume(T_EQ,"expect '=' after loop variable");
    expression();
    emit("mov %%rax, %d(%%rbp)", iv->offset);
    consume(T_TO,"expect 'to'");
    /* store limit in a hidden local */
    char hidden[64]; snprintf(hidden,sizeof(hidden)," limit%d", next_label());
    Local *lim=add_local(cur_func,hidden,(int)strlen(hidden));
    expression();
    emit("mov %%rax, %d(%%rbp)", lim->offset);

    int top=next_label(), end=next_label();
    emit_label(".L%d", top);
    emit("mov %d(%%rbp), %%rax", iv->offset);
    emit("mov %d(%%rbp), %%rcx", lim->offset);
    emit("cmp %%rcx, %%rax");
    emit("jg .L%d", end);
    while(!check(T_END)&&!check(T_EOF)) statement();
    emit("mov %d(%%rbp), %%rax", iv->offset);
    emit("add $1, %%rax");
    emit("mov %%rax, %d(%%rbp)", iv->offset);
    emit("jmp .L%d", top);
    emit_label(".L%d", end);
    consume(T_END,"expect 'end' after for");
    match(T_FOR);
}

static void return_statement(void){
    if(check(T_END)||check(T_EOF)){
        emit("mov $0, %%rax");
    } else {
        expression();
    }
    emit("jmp .Lret_%s", cur_func->name);
}

static void asm_statement(void){
    consume(T_STRING,"expect string after asm");
    /* pass raw text straight through to the .text section */
    str_add(&TEXT,"    %.*s\n", prev.length, prev.start);
}

static void statement(void){
    if(match(T_PRINT)) print_statement();
    else if(match(T_VAR)) var_declaration();
    else if(match(T_IF)) if_statement();
    else if(match(T_WHILE)) while_statement();
    else if(match(T_FOR)) for_statement();
    else if(match(T_RETURN)) return_statement();
    else if(match(T_ASM)) asm_statement();
    else assignment_or_expr_statement();
}

/* ======================================================================== */
/*  Function compilation                                                     */
/*    Two passes conceptually: we register the function signature, then      */
/*    emit its prologue/body/epilogue.                                       */
/* ======================================================================== */

static void compile_function(void){
    consume(T_IDENT,"expect function name");
    Token name=prev;
    Func *f=&funcs[func_count++];
    int n=name.length<63?name.length:63;
    memcpy(f->name,name.start,n); f->name[n]='\0';
    f->arity=0; f->local_count=0; f->frame_size=0;
    cur_func=f;

    consume(T_LPAREN,"expect '(' after function name");
    /* parameters become the first locals */
    if(!check(T_RPAREN)){
        do{
            consume(T_IDENT,"expect parameter name");
            add_local(f, prev.start, prev.length);
            f->arity++;
        }while(match(T_COMMA));
    }
    consume(T_RPAREN,"expect ')' after parameters");

    /* Build the body into its own buffer by swapping the global TEXT.
       We save the real TEXT, point TEXT at a fresh body buffer, emit into it,
       then restore and append. Because emit() calls str_add(&TEXT,...) which
       may realloc, we operate on the global struct directly and copy back. */
    Str real_text = TEXT;          /* snapshot current main stream */
    Str body; str_init(&body);
    TEXT = body;                   /* redirect emits into body */

    /* move incoming arg registers into their local slots */
    for(int i=0;i<f->arity;i++)
        emit("mov %s, %d(%%rbp)", ARG_REGS[i], f->locals[i].offset);

    while(!check(T_END)&&!check(T_EOF)) statement();

    consume(T_END,"expect 'end' after function body");
    match(T_FUNC);

    /* default return 0 */
    emit("mov $0, %%rax");
    emit_label(".Lret_%s", f->name);

    body = TEXT;         /* body buffer (possibly realloc'd) */
    TEXT = real_text;    /* restore main stream */

    /* now emit the real function with correct frame size (16-byte aligned) */
    int frame=(f->frame_size+15)&~15;
    const char *sym = (strcmp(f->name,"main")==0) ? "mike_main" : f->name;
    str_add(&TEXT,"\n.globl %s\n%s:\n", sym, sym);
    emit("push %%rbp");
    emit("mov %%rsp, %%rbp");
    if(frame>0) emit("sub $%d, %%rsp", frame);
    str_add(&TEXT,"%s",body.buf);
    emit("mov %%rbp, %%rsp");
    emit("pop %%rbp");
    emit("ret");
    free(body.buf);
}

/* ======================================================================== */
/*  Module parsing                                                           */
/* ======================================================================== */

static void parse_module(void){
    consume(T_START,"expect 'start'");
    consume(T_MODULE,"expect 'module'");
    match(T_SEMI);
    if(check(T_IDENT)) advance();
    if(match(T_LIB)){
        consume(T_IDENT,"expect lib name");
        if(match(T_SLASH)) consume(T_IDENT,"expect lib name after '/'");
    }
    while(!check(T_END)&&!check(T_EOF)){
        if(match(T_FUNC)) compile_function();
        else { error("only function declarations allowed at module level"); advance(); }
    }
    consume(T_END,"expect 'end modul'");
    consume(T_MODUL,"expect 'modul'");
}

/* ======================================================================== */
/*  Runtime prelude — hand-written assembly, emitted once                    */
/*    _start, print_int, print_newline, exit                                 */
/* ======================================================================== */

static const char *RUNTIME =
"# ---- runtime prelude (no libc) ----\n"
".globl _start\n"
"_start:\n"
"    call mike_main\n"
"    mov %rax, %rdi        # exit code = main's return\n"
"    mov $60, %rax         # sys_exit\n"
"    syscall\n"
"\n"
"# print_newline: write '\\n' to stdout\n"
"print_newline:\n"
"    push %rbp\n"
"    mov %rsp, %rbp\n"
"    sub $16, %rsp\n"
"    movb $10, -1(%rbp)\n"
"    mov $1, %rax\n"
"    mov $1, %rdi\n"
"    lea -1(%rbp), %rsi\n"
"    mov $1, %rdx\n"
"    syscall\n"
"    mov %rbp, %rsp\n"
"    pop %rbp\n"
"    ret\n"
"\n"
"# print_int: signed 64-bit integer in %rdi -> stdout (no newline)\n"
"print_int:\n"
"    push %rbp\n"
"    mov %rsp, %rbp\n"
"    sub $32, %rsp\n"
"    mov %rdi, %rax        # value\n"
"    lea -1(%rbp), %rsi    # point to end of buffer\n"
"    mov $10, %rcx         # divisor\n"
"    mov $0, %r8           # digit count\n"
"    mov $0, %r9           # negative flag\n"
"    cmp $0, %rax\n"
"    jge .Lpi_conv\n"
"    mov $1, %r9\n"
"    neg %rax\n"
".Lpi_conv:\n"
"    cqo\n"
"    idiv %rcx             # rax=quotient, rdx=remainder\n"
"    add $48, %rdx         # to ASCII\n"
"    mov %dl, (%rsi)\n"
"    dec %rsi\n"
"    inc %r8\n"
"    cmp $0, %rax\n"
"    jne .Lpi_conv\n"
"    cmp $1, %r9           # add '-' if negative\n"
"    jne .Lpi_write\n"
"    movb $45, (%rsi)\n"
"    dec %rsi\n"
"    inc %r8\n"
".Lpi_write:\n"
"    inc %rsi              # rsi -> first char\n"
"    mov $1, %rax          # sys_write\n"
"    mov $1, %rdi\n"
"    mov %r8, %rdx         # length\n"
"    syscall\n"
"    mov %rbp, %rsp\n"
"    pop %rbp\n"
"    ret\n"
"\n"
"# read_int: read chars from stdin until newline/EOF, parse signed decimal -> %rax\n"
"read_int:\n"
"    push %rbp\n"
"    mov %rsp, %rbp\n"
"    sub $16, %rsp          # 1-byte read buffer at -1(%rbp)\n"
"    mov $0, %r8            # r8 = accumulated value\n"
"    mov $0, %r9            # r9 = negative flag\n"
"    mov $0, %r10           # r10 = seen-any-digit flag\n"
".Lri_next:\n"
"    mov $0, %rax           # sys_read\n"
"    mov $0, %rdi           # fd = stdin\n"
"    lea -1(%rbp), %rsi     # 1-byte buffer\n"
"    mov $1, %rdx           # read exactly one byte\n"
"    syscall\n"
"    cmp $0, %rax           # EOF?\n"
"    jle .Lri_done\n"
"    movzbq -1(%rbp), %rcx  # the char\n"
"    cmp $10, %rcx          # newline -> stop\n"
"    je .Lri_done\n"
"    cmp $45, %rcx          # '-'\n"
"    jne .Lri_digit\n"
"    cmp $0, %r10           # only treat as sign if no digits yet\n"
"    jne .Lri_next\n"
"    mov $1, %r9\n"
"    jmp .Lri_next\n"
".Lri_digit:\n"
"    cmp $48, %rcx          # below '0'?\n"
"    jl .Lri_next           # ignore non-digits (spaces etc.)\n"
"    cmp $57, %rcx          # above '9'?\n"
"    jg .Lri_next\n"
"    sub $48, %rcx          # digit value\n"
"    imul $10, %r8\n"
"    add %rcx, %r8\n"
"    mov $1, %r10           # saw a digit\n"
"    jmp .Lri_next\n"
".Lri_done:\n"
"    mov %r8, %rax\n"
"    cmp $1, %r9\n"
"    jne .Lri_ret\n"
"    neg %rax\n"
".Lri_ret:\n"
"    mov %rbp, %rsp\n"
"    pop %rbp\n"
"    ret\n";

/* ======================================================================== */
/*  Driver                                                                   */
/* ======================================================================== */

static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"mike: cannot open %s\n",path); exit(74); }
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    char *b=malloc(sz+1);
    if(fread(b,1,sz,f)!=(size_t)sz){ fprintf(stderr,"mike: read error\n"); exit(74); }
    b[sz]='\0'; fclose(f); return b;
}

int main(int argc,char**argv){
    bool asm_only=false;
    const char *path=NULL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-S")==0) asm_only=true;
        else path=argv[i];
    }
    if(!path){ fprintf(stderr,"usage: mike [-S] program.mik\n"); return 64; }

    char *src=read_file(path);
    str_init(&TEXT); str_init(&DATA);
    lexer_init(src);
    advance();
    parse_module();
    if(!check(T_EOF)) error("expected end of file");

    if(!find_func("main",4)){ fprintf(stderr,"mike: no main() function\n"); return 65; }
    if(had_error){ return 65; }

    /* assemble the full .s file: runtime + generated text + data (AT&T) */
    Str out; str_init(&out);
    str_add(&out,".text\n");
    str_add(&out,"%s\n",RUNTIME);
    str_add(&out,"%s\n",TEXT.buf);
    str_add(&out,".data\n");
    str_add(&out,"%s\n",DATA.buf);

    /* derive base name (strip dir + .mik) */
    char *pcopy=strdup(path);
    char *base=basename(pcopy);
    char stem[256]; strncpy(stem,base,sizeof(stem)-1); stem[sizeof(stem)-1]='\0';
    char *dot=strrchr(stem,'.'); if(dot) *dot='\0';

    char sfile[300]; snprintf(sfile,sizeof(sfile),"%s.s",stem);
    FILE *sf=fopen(sfile,"w");
    if(!sf){ fprintf(stderr,"mike: cannot write %s\n",sfile); return 74; }
    fputs(out.buf,sf); fclose(sf);

    if(asm_only){
        printf("wrote %s\n",sfile);
        return 0;
    }

    /* assemble + link with as and ld (no C, no libc) */
    char ofile[300]; snprintf(ofile,sizeof(ofile),"%s.o",stem);
    char cmd[900];
    snprintf(cmd,sizeof(cmd),"as --64 -o %s %s",ofile,sfile);
    if(system(cmd)!=0){ fprintf(stderr,"mike: assembly failed\n"); return 70; }
    snprintf(cmd,sizeof(cmd),"ld -o %s %s",stem,ofile);
    if(system(cmd)!=0){ fprintf(stderr,"mike: link failed\n"); return 70; }

    /* clean up intermediates */
    remove(sfile); remove(ofile);

    printf("compiled %s -> ./%s\n",path,stem);
    return 0;
}
