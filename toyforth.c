#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define TFDA_INIT_CAP 256
#define TFLIST_INIT_CAP 16

#define ARRAY_LEN(array) (sizeof(array)/sizeof(array[0]))

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if(ptr == NULL) {
        fprintf(stderr, "ERROR: Could not allocate memory");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *nptr = realloc(ptr, size);
    if(nptr == NULL) {
        fprintf(stderr, "ERROR: Could not allocate memory");
        exit(1);
    }
    return nptr;
}

/* ==================== Dynamic array DS ==================== */
typedef struct {
    // int refcount;
    size_t count;
    size_t capacity;
} tfdahdr;

tfdahdr *tfdaHeader(void *da) {
    tfdahdr *header = &((tfdahdr *) da)[-1];
    return header;
}

size_t tfdaCount(void *da) {
    tfdahdr *header = tfdaHeader(da);
    return header->count;
}

void tfdaSetCount(void *da, size_t count) {
    tfdahdr *header = tfdaHeader(da);
    header->count = count;
}


void* tfdaReserve(void *da, size_t tsize, size_t len) {
    size_t newcap = TFDA_INIT_CAP;
    size_t reserve = len;
    tfdahdr *header = NULL;
    if(da != NULL) {
        header = tfdaHeader(da);
        if(header->capacity > header->count + len) return da;
        newcap = header->capacity;
        reserve = len + header->count;
    }
    while(newcap < reserve) newcap *= 2;
    header = xrealloc(header, newcap*tsize + sizeof(tfdahdr));
    header->capacity = newcap;
    return ((char*) header) + sizeof(tfdahdr);
}

char *tfdaStringCat(char *da, char *s, size_t len) {
    tfdahdr *header;
    da = tfdaReserve(da, sizeof(char), len);
    header = tfdaHeader(da);
    memcpy(&da[header->count], s, len);
    header->count += len;
    return da;
}

/* ==================== Parser related structures ==================== */

typedef enum tftype {
    TFOBJ_TYPE_INT,
    TFOBJ_TYPE_BOOL,
    TFOBJ_TYPE_STR,
    TFOBJ_TYPE_LIST,
    TFOBJ_TYPE_SYMBOL,
    TFOBJ_TYPE_COUNT,
} tftype;

typedef struct tfobj {
    int refcount;
    tftype type;
    union {
        int i;
        struct {
            char *ptr;
            size_t len;
        } str;
        // List implemented as a tfda structure
        struct tfobj **list;
    };
} tfobj;

void derefObject(tfobj *o);

tfobj* createObject(int type) {
    tfobj *o = xmalloc(sizeof(tfobj));
    o->type = type;
    o->refcount = 1;
    return o;
}

tfobj* createIntObject(int i) {
    tfobj *o = createObject(TFOBJ_TYPE_INT);
    o->i = i;
    return o;
}

tfobj* createBoolObject(int i) {
    tfobj *o = createObject(TFOBJ_TYPE_BOOL);
    o->i = i;
    return o;
}

tfobj* createStringObject(char *s, size_t len) {
    tfobj *o = createObject(TFOBJ_TYPE_STR);
    o->str.ptr = s;
    o->str.len = len;
    return o;
}

tfobj* createSymbolObject(char *s, size_t len) {
    tfobj *o = createStringObject(s, len);
    o->type = TFOBJ_TYPE_SYMBOL;
    return o;
}

tfobj* createListObject() {
    tfobj *o = createObject(TFOBJ_TYPE_LIST);
    o->list = NULL;
    return o;
}

/* listAppendObject - append a new object to the list and increment the refcount of the object */
void listAppendObject(tfobj *parent, tfobj *val) {
    assert(parent->type == TFOBJ_TYPE_LIST);
    parent->list = tfdaReserve(parent->list, sizeof(tfobj), 1);

    size_t count = tfdaCount(parent->list);

    parent->list[count] = val;
    tfdaSetCount(parent->list, count+1);
    val->refcount++;
}

/* listGetObject - returns the value of a tfobj* and increase the refcount */
tfobj* listGetObject(tfobj *parent, int index) {
    assert(parent->type == TFOBJ_TYPE_LIST);

    size_t count = tfdaCount(parent->list);
    assert(count > 0);

    tfobj *ret = parent->list[index];
    ret->refcount++;
    return ret;
}

tfobj* listPopObject(tfobj *parent) {
    assert(parent->type == TFOBJ_TYPE_LIST);

    size_t count = tfdaCount(parent->list);
    assert(count > 0);

    tfobj *ret = parent->list[count-1];
    parent->list[count-1] = NULL;
    tfdaSetCount(parent->list, count-1);
    return ret;
}

void printObject(tfobj *obj, int indent) {

    for(int i = 0; i < indent; i++) printf(" ");

    switch(obj->type) {
        case TFOBJ_TYPE_INT:
        case TFOBJ_TYPE_BOOL:
            printf("%d\n", obj->i);
            break;
        case TFOBJ_TYPE_STR:
            printf("%.*s\n", (int)obj->str.len, obj->str.ptr);
            break;
        case TFOBJ_TYPE_LIST:
            printf("[\n");
            for(int i = 0; i < tfdaCount(obj->list); i++) {
                printObject(obj->list[i], indent+2);
            }
            for(int i = 0; i < indent; i++) printf(" ");
            printf("]\n");
            break;
        default:
            fprintf(stderr, "printOperation: invalid type value");
            exit(1);
    }
}


void freeObject(tfobj *o) {
    switch(o->type) {
        case TFOBJ_TYPE_INT:
        case TFOBJ_TYPE_BOOL:
            free(o);
            break;
        case TFOBJ_TYPE_LIST:
        {
            for(int i = 0; i < tfdaCount(o->list); i++) derefObject(o->list[i]);
        } break;
        default:
            printf("Not handled values");
            exit(0);
    }
}

void derefObject(tfobj *o) {
    assert(o->refcount > 0 && "Called deref on an already freed object");
    o->refcount--;
    if(o->refcount == 0) {
        freeObject(o);
    }
}

typedef struct tfparser {
    char *start;
    char *end;
    char *p;
} tfparser;

typedef struct tfctx {
    tfobj *stack;
} tfctx;


void evalList(tfctx *ctx, tfobj *l);

/* ==================== Definition of symbols operations ==================== */
#define TFSYMOP_FUNCTIONS(name) void name(tfctx *ctx)

TFSYMOP_FUNCTIONS(addOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "addOperation support only int types");
    tfobj *result = createIntObject(op1->i+op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(subOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "subOperation support only int types");
    tfobj *result = createIntObject(op1->i-op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(mulOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "mulOperation support only int types");
    tfobj *result = createIntObject(op1->i*op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(divOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "divOperation support only int types");
    tfobj *result = createIntObject(op1->i/op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(lessOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "lessOperation support only int types");
    tfobj *result = createBoolObject(op1->i < op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(greaterOperation) {
    tfobj *op2 = listPopObject(ctx->stack);
    tfobj *op1 = listPopObject(ctx->stack);

    assert(op1->type == TFOBJ_TYPE_INT && op1->type == op2->type && "greaterOperation support only int types");
    tfobj *result = createBoolObject(op1->i > op2->i);

    listAppendObject(ctx->stack, result);
    derefObject(result);
    derefObject(op1);
    derefObject(op2);
}

TFSYMOP_FUNCTIONS(ifOperation) {
    tfobj *then = listPopObject(ctx->stack);
    tfobj *cond = listPopObject(ctx->stack);

    if(cond->type == TFOBJ_TYPE_LIST) {
        evalList(ctx, cond);
        derefObject(cond);
        cond = listPopObject(ctx->stack);
    }

    assert(cond->type == TFOBJ_TYPE_BOOL && "ERROR: if condition does not result in a boolean type");
    assert(then->type == TFOBJ_TYPE_LIST && "ERROR: if condition does not result in a boolean type");
    if(cond->i) evalList(ctx, then);

    derefObject(cond);
    derefObject(then);
}

TFSYMOP_FUNCTIONS(dupOperation) {
    tfobj *obj = listPopObject(ctx->stack);

    listAppendObject(ctx->stack, obj);
    listAppendObject(ctx->stack, obj);
    derefObject(obj);
}

TFSYMOP_FUNCTIONS(swapOperation) {
    tfobj *obj1 = listPopObject(ctx->stack);
    tfobj *obj2 = listPopObject(ctx->stack);

    listAppendObject(ctx->stack, obj1);
    listAppendObject(ctx->stack, obj2);
    derefObject(obj1);
    derefObject(obj2);
}

TFSYMOP_FUNCTIONS(rotOperation) {
    tfobj *obj3 = listPopObject(ctx->stack);
    tfobj *obj2 = listPopObject(ctx->stack);
    tfobj *obj1 = listPopObject(ctx->stack);

    listAppendObject(ctx->stack, obj2);
    listAppendObject(ctx->stack, obj3);
    listAppendObject(ctx->stack, obj1);

    derefObject(obj1);
    derefObject(obj2);
    derefObject(obj3);
}

TFSYMOP_FUNCTIONS(overOperation) {
    tfobj *obj2 = listPopObject(ctx->stack);
    tfobj *obj1 = listPopObject(ctx->stack);

    listAppendObject(ctx->stack, obj1);
    listAppendObject(ctx->stack, obj2);
    listAppendObject(ctx->stack, obj1);

    derefObject(obj1);
    derefObject(obj2);
}

TFSYMOP_FUNCTIONS(dropOperation) {
    tfobj *obj = listPopObject(ctx->stack);
    derefObject(obj);
}

TFSYMOP_FUNCTIONS(printOperation) {
    tfobj *obj = listPopObject(ctx->stack);

    printObject(obj, 0);

    derefObject(obj);
}



typedef TFSYMOP_FUNCTIONS(tfsymop_function);
struct {
    char *str;
    int len;

    tfsymop_function *func;
} tfsymop[] = {
    { .str = "+",     .len = 1, addOperation},
    { .str = "-",     .len = 1, subOperation},
    { .str = "*",     .len = 1, mulOperation},
    { .str = "/",     .len = 1, divOperation},
    { .str = "<",     .len = 1, lessOperation},
    { .str = ">",     .len = 1, greaterOperation},
    { .str = "if",    .len = 2, ifOperation},
    { .str = "dup",   .len = 3, dupOperation},
    { .str = "rot",   .len = 3, rotOperation},
    { .str = "over",  .len = 4, overOperation},
    { .str = "swap",  .len = 4, swapOperation},
    { .str = "drop",  .len = 4, dropOperation},
    { .str = "print", .len = 5, printOperation},
};

tfsymop_function* searchSymbolOperation(tfobj *obj) {
    assert(obj->type == TFOBJ_TYPE_SYMBOL && "searchSymbolOperation: passed wrong type");
    for(int i = 0; i < ARRAY_LEN(tfsymop); i++) {
        if(obj->str.len != tfsymop[i].len) continue;
        if(strncmp(obj->str.ptr, tfsymop[i].str, obj->str.len) == 0)
            return tfsymop[i].func;
    }

    fprintf(stderr, "ERROR: searchSymbolOperation: Unrecognized symbol '%.*s' found", (int)obj->str.len, obj->str.ptr);
    exit(1);
}


/* readEntireFile returns a string created with tfda* utilities */
char *readEntireFile(char *filename) {
    char buf[1024];
    char *prg = NULL;

    FILE *fin = fopen(filename, "r");
    size_t nread;

    while((nread = fread(buf, sizeof(*buf), sizeof(buf), fin)) > 0) {
        prg = tfdaStringCat(prg, buf, nread);
    }
    return prg;
}

void parserTrimLeft(tfparser *parser) {
    while(parser->p < parser->end && isspace(*parser->p)) parser->p++;
}

bool issymbolStart(int ch) {
    return isalnum(ch) || ch == '_';
}

tfobj *parseObject(tfparser *parser) {
    parserTrimLeft(parser);
    if(parser->p >= parser->end) return NULL;
    if(isdigit(*parser->p)) {
        int num = 0;
        char *saved_point = parser->p;
        // TODO: verify support for hex values
        while(isdigit(*parser->p)) {
            num = num*10 + *parser->p - '0';
            parser->p++;
        }
        if(!isspace(*parser->p)) {
            fprintf(stderr, "ERROR: not a valid number value %.*s", (int) (parser->p - saved_point), saved_point);
            parser->p = saved_point;
            return NULL;
        }

        return createIntObject(num);
    }
    if(*parser->p == '[') {
        char *saved_point = parser->p;
        parser->p++;
        tfobj *list = createListObject();
        while(parser->p < parser->end && *parser->p != ']') {
            parserTrimLeft(parser);
            if(*parser->p == ']') break;

            tfobj *obj = parseObject(parser);
            if(obj != NULL) {
                listAppendObject(list, obj);
                derefObject(obj);
            }
        }
        parserTrimLeft(parser);
        if(*parser->p != ']') {
            fprintf(stderr, "ERROR: not a valid list type %.*s", (int) (parser->p - saved_point), saved_point);
            parser->p = saved_point;
            exit(1);
        }

        parser->p++;
        return list;
    }
    
    // Check to see if it is one of the default symbols
    for(int i = 0; i < ARRAY_LEN(tfsymop); i++) {
        char *sym = tfsymop[i].str;
        int len = tfsymop[i].len;
        if(parser->p + len < parser->end && strncmp(parser->p, sym, len) == 0) {
            // Consume the symbol length
            tfobj *obj = createSymbolObject(parser->p, len);
            parser->p += len;
            return obj;
        }
    }
    if(issymbolStart(*parser->p)) {
        char *saved_point = parser->p;
        while(isalnum(*parser->p) || ispunct(*parser->p)) parser->p++;

        if(!isspace(*parser->p)) {
            fprintf(stderr, "ERROR: not a valid symbol value %.*s\n", (int) (parser->p - saved_point), saved_point);
            parser->p = saved_point;
            return NULL;
        }

        if(strncmp(saved_point, "true", strlen("true")) == 0) return createBoolObject(true);
        if(strncmp(saved_point, "false", strlen("false")) == 0) return createBoolObject(false);

        return createSymbolObject(saved_point, (int) (parser->p - saved_point));
    }
    printf("TODO: parseObject: handle other types\n");
    exit(0);
    return NULL;
}

tfobj* compile(char *prgtext, size_t len) {
    tfobj *prg = createListObject();
    tfparser parser = { .start = prgtext, .end = prgtext + len, .p = prgtext };
    while(parser.p < parser.end) {
        tfobj *obj = parseObject(&parser);
        if(obj != NULL) {
            listAppendObject(prg, obj);
            derefObject(obj);
        }
    }
    return prg;
}

void eval(tfctx *ctx, tfobj *obj) {
    switch(obj->type) {
        case TFOBJ_TYPE_LIST:
        case TFOBJ_TYPE_INT:
        case TFOBJ_TYPE_BOOL:
        {
            listAppendObject(ctx->stack, obj);
        } break;
        case TFOBJ_TYPE_SYMBOL:
        {
            tfsymop_function *func = searchSymbolOperation(obj);
            func(ctx);
        } break;
        default:
            fprintf(stderr, "ERROR: value not handled\n");
            exit(1);
    }
}

void evalList(tfctx *ctx, tfobj *l) {
    for(int i = 0; i < tfdaCount(l->list); i++) {
        tfobj *obj = listGetObject(l, i);
        eval(ctx, obj);
        derefObject(obj);
    }
}

void exec(tfobj *prg) {
    tfctx ctx = { .stack = createListObject() };
    assert(prg->type == TFOBJ_TYPE_LIST && "Cannot exec something that is not a list of objects");

    evalList(&ctx, prg);
}

int main(int argc, char **argv) {
    if(argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    char *filename = argv[1];
    char *prgtext = readEntireFile(filename);
    // printf("%s", prgtext);

    tfobj *prg = compile(prgtext, tfdaCount(prgtext));
    exec(prg);

    return 0;
}
