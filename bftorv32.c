#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum TokenKind {
    TK_PTR_L, // 0
    TK_PTR_R, // 1
    TK_ADD, // 2
    TK_SUB, // 3
    TK_LOOP_S, // 4
    TK_LOOP_E, // 5
    TK_IN, // 6
    TK_OUT, // 7
    TK_EOF, // 8
} TokenKind;

typedef struct Token Token;
typedef struct Token {
    TokenKind kind;
    Token* next;
    char* loc;
    uint32_t len;
    uint32_t insts_to_branch; // for loop ends
} Token;

uint32_t gen_beq_op(int32_t offset) {
    uint32_t inst = 0;
    inst |= 0x63;
    inst |= (offset & 0x800) >> 4;
    inst |= (offset & 0x1e) << 7;
    inst |= 0x1e << 15;
    inst |= 0x0 << 20;
    inst |= (offset & 0x7e0) << 20;
    inst |= (offset & 0x1000) << 19;
    return inst;
}

uint32_t gen_bne_op(int32_t offset) {
    uint32_t inst = 0;
    inst |= 0x63;
    inst |= 1 << 12;
    inst |= (offset & 0x800) >> 4;
    inst |= (offset & 0x1e) << 7;
    inst |= 0x1e << 15;
    inst |= 0x0 << 20;
    inst |= (offset & 0x7e0) << 20;
    inst |= (offset & 0x1000) << 19;
    return inst;
}

uint32_t gen_addi_op(uint32_t reg, int32_t imm) {
    uint32_t inst = 0x00000013;
    inst |= reg << 7;
    inst |= reg << 15;
    inst |= ((imm & 0xfff) << 20);
    return inst;
}

static Token* new_token(TokenKind kind, char* start, char* end) {
    Token* tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    tok->insts_to_branch = 0;
    return tok;
}

static Token* tokenize(char* p) {
    Token head = {};
    Token* cur = &head;
    char* s = p;
    // printf("\n");
    while(*p) {
        // printf("DEBUG: at character %lu\n", p - s);
        TokenKind kind;
        char inst;
        switch(*p) {
            case '<': kind = TK_PTR_L; inst = '<'; break;
            case '>': kind = TK_PTR_R; inst = '>'; break;
            case '+': kind = TK_ADD; inst = '+'; break;
            case '-': kind = TK_SUB; inst = '-'; break;
            case '[': kind = TK_LOOP_S; inst = '['; break;
            case ']': kind = TK_LOOP_E; inst = ']'; break;
            case ',': kind = TK_IN; inst = ','; break;
            case '.': kind = TK_OUT; inst = '.'; break;
            default: p++; continue;
        }
        // printf("DEBUG: type %d\n", kind);
        cur = cur->next = new_token(kind, p, p);
        char* q = p;
        if(inst != '[' && inst != ']') {
            while(true) {
                if(*p != inst) break;
                p++;
            }
        } else {
            p++;
        }
        cur->len = p - q;
        // printf("DEBUG: len of %u\n", cur->len);
    }
    //printf("\n");
    cur = cur->next = new_token(TK_EOF, p, p);
    return head.next;
}

int main(int argc, char **argv) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s, <bf file> <output file>\n", argv[0]);
        return 1;
    }
    FILE* bf = fopen(argv[1], "rb");
    if(bf == NULL) {
        fprintf(stderr, "Couldn't open bf file %s\n", argv[1]);
        return 1;
    }
    fseek(bf, 0, SEEK_END);
    unsigned long fileLen = ftell(bf);
    fseek(bf, 0, SEEK_SET);
    // printf("\nDEBUG: fileLen: %lu\n", fileLen);
    char* code = (char*)malloc(fileLen+1);
    fread(code, fileLen, 1, bf);
    fclose(bf);
    Token* tok = tokenize(code);
    Token* mtok = tok;
    //free(code);
    FILE* outfile = fopen(argv[2], "wb");
    if(outfile == NULL) {
        fprintf(stderr, "Couldn't open output file %s\n", argv[2]);
        free(code);
        return 1;
    }
    uint32_t inst = 0x10000e37;
    fwrite(&inst, sizeof(uint32_t), 1, outfile);
    inst = 0x80000eb7;
    fwrite(&inst, sizeof(uint32_t), 1, outfile);
    uint32_t total_inst_count = 3;
    while(mtok->kind != TK_EOF) {
        // printf("DEBUG: type %d\n", mtok->kind);
        switch(mtok->kind) {
            case TK_LOOP_S:
            case TK_LOOP_E:
            case TK_IN:
            case TK_OUT:
                total_inst_count += 2;
                break;
            case TK_PTR_L:
            case TK_PTR_R:
                total_inst_count += 1;
                break;
            case TK_SUB:
            case TK_ADD:
                total_inst_count += 3;
                break;
            default: break;
        }
        mtok = mtok->next;
    }
    printf("total_inst_count = %u\n", total_inst_count);
    uint32_t gen_inst = gen_addi_op(0x1d, (total_inst_count * sizeof(uint32_t)) + 1);
    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
    while(tok->kind != TK_EOF) {
        switch(tok->kind) {
            case TK_PTR_L:
                gen_inst = gen_addi_op(0x1d, -(tok->len * sizeof(uint32_t)));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_PTR_R:
                gen_inst = gen_addi_op(0x1d, tok->len * sizeof(uint32_t));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_ADD:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_addi_op(0x1e, tok->len);
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01eea023;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_SUB:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_addi_op(0x1e, -(tok->len));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01eea023;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_LOOP_S:
            {
                
                uint32_t loop_counter = 0;
                uint32_t insts = 0;
                mtok = tok;
                while(true) {
                    if(mtok->kind == TK_LOOP_S) {
                        loop_counter += 1;
                        if(loop_counter != 1) insts += 2;
                    } else if(mtok->kind == TK_LOOP_E) {
                        loop_counter -= 1;
                        insts += 2;
                        if(loop_counter == 0) {
                            mtok->insts_to_branch = insts - 1;
                            break;
                        }
                    } else {
                        switch(mtok->kind) {
                            case TK_IN:
                            case TK_OUT:
                                insts += 2;
                                break;
                            case TK_PTR_L:
                            case TK_PTR_R:
                                insts += 1;
                                break;
                            case TK_SUB:
                            case TK_ADD:
                                insts += 3;
                                break;
                            default: break;
                        }
                    }
                    mtok = mtok->next;
                }
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_beq_op(insts * sizeof(uint32_t));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            }
            case TK_LOOP_E:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_bne_op(-(tok->insts_to_branch * sizeof(uint32_t)));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_IN:
                inst = 0x000e2f03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01eea023;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_OUT:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01ee2023;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                break;
            default: break;
        }
        tok = tok->next;
    }
    fclose(outfile);
    free(code);
    return 0;
}
