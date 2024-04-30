#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum TokenKind {
    TK_PTR, // 0
    TK_CELL, // 1
    TK_LOOP_S, // 2
    TK_LOOP_E, // 3
    TK_IN, // 4
    TK_OUT, // 5
    TK_EOF, // 6
} TokenKind;

typedef struct Token Token;
typedef struct Token {
    TokenKind kind;
    Token* next;
    char* loc;
    int32_t len;
    bool is32;
    uint32_t insts_to_branch; // for loop ends
} Token;

uint32_t gen_beq_op(uint32_t rs1, uint32_t rs2, int32_t offset) {
    uint32_t inst = 0;
    inst |= 0x63;
    inst |= (offset & 0x800) >> 4;
    inst |= (offset & 0x1e) << 7;
    inst |= rs1 << 15;
    inst |= rs2 << 20;
    inst |= (offset & 0x7e0) << 20;
    inst |= (offset & 0x1000) << 19;
    return inst;
}

uint32_t gen_bne_op(uint32_t rs1, uint32_t rs2, int32_t offset) {
    uint32_t inst = 0;
    inst |= 0x63;
    inst |= 1 << 12;
    inst |= (offset & 0x800) >> 4;
    inst |= (offset & 0x1e) << 7;
    inst |= rs1 << 15;
    inst |= rs2 << 20;
    inst |= (offset & 0x7e0) << 20;
    inst |= (offset & 0x1000) << 19;
    return inst;
}

uint32_t gen_lui_op(uint32_t rd, uint32_t imm) {
    uint32_t inst = 0x00000037;
    inst |= rd << 7;
    inst |= imm & 0xfffff000;
    return inst;
}

uint32_t gen_add_op(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    uint32_t inst = 0x00000033;
    inst |= rd << 7;
    inst |= rs1 << 15;
    inst |= rs2 << 20;
    return inst;
}

uint32_t gen_addi_op(uint32_t rd, uint32_t rs1, int32_t imm) {
    uint32_t inst = 0x00000013;
    inst |= rd << 7;
    inst |= rs1 << 15;
    inst |= ((imm & 0xfff) << 20);
    return inst;
}

static Token* new_token(TokenKind kind, char* start, char* end) {
    Token* tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    tok->is32 = false;
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
        char inst = *p;
        switch(*p) {
            case '<':
            case '>':
                kind = TK_PTR;
                break;
            case '+':
            case '-':
                kind = TK_CELL;
                break;
            case '[': kind = TK_LOOP_S; break;
            case ']': kind = TK_LOOP_E; break;
            case ',': kind = TK_IN; break;
            case '.': kind = TK_OUT; break;
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
        if(inst == '-') {
            cur->len = -(p - q);
        } else {
            cur->len = p - q;
        }
        if(kind == TK_PTR) {
            if(inst == '<') cur->len = -(cur->len * sizeof(uint32_t));
            else cur->len *= sizeof(uint32_t);
        }
        if(cur->len < -2048 || cur->len > 2047) { // exceeds signed 12-bit integer maximum
            cur->is32 = true;
        }
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
            case TK_PTR:
                if(mtok->is32) {
                    total_inst_count += 3;
                } else {
                    total_inst_count += 1;
                }
                break;
            case TK_CELL:
                if(mtok->is32) {
                    total_inst_count += 5;
                } else {
                    total_inst_count += 3;
                }
                break;
            default: break;
        }
        mtok = mtok->next;
    }
    printf("total_inst_count = %u\n", total_inst_count);
    uint32_t gen_inst = 0;
    if(((total_inst_count * sizeof(uint32_t)) + 1) > 4095) {
        total_inst_count += 2;
        gen_inst = gen_lui_op(0x1e, (total_inst_count * sizeof(uint32_t)) + 1);
        fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
        gen_inst = gen_addi_op(0x1e, 0x1e, (total_inst_count * sizeof(uint32_t)) + 1);
        fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
        gen_inst = gen_add_op(0x1d, 0x1d, 0x1e);
        fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
    } else {
        gen_inst = gen_addi_op(0x1d, 0x1d, (total_inst_count * sizeof(uint32_t)) + 1);
        fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
    }
    while(tok->kind != TK_EOF) {
        switch(tok->kind) {
            case TK_PTR:
                if(tok->is32) {
                    gen_inst = gen_lui_op(0x1e, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                    gen_inst = gen_addi_op(0x1e, 0x1e, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                    gen_inst = gen_add_op(0x1d, 0x1d, 0x1e);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                } else {
                    gen_inst = gen_addi_op(0x1d, 0x1d, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                }
                break;
            case TK_CELL:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                if(tok->is32) {
                    gen_inst = gen_lui_op(0x1f, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                    gen_inst = gen_addi_op(0x1f, 0x1f, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                    gen_inst = gen_add_op(0x1e, 0x1e, 0x1f);
                } else {
                    gen_inst = gen_addi_op(0x1e, 0x1e, tok->len);
                    fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                }
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
                            case TK_PTR:
                                if(mtok->is32) {
                                    insts += 3;
                                } else {
                                    insts += 1;
                                }
                                break;
                            case TK_CELL:
                                if(mtok->is32) {
                                    insts += 5;
                                } else {
                                    insts += 3;
                                }
                                break;
                            default: break;
                        }
                    }
                    mtok = mtok->next;
                }
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_beq_op(0x1e, 0x0, insts * sizeof(uint32_t));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            }
            case TK_LOOP_E:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                gen_inst = gen_bne_op(0x1e, 0x0, -(tok->insts_to_branch * sizeof(uint32_t)));
                fwrite(&gen_inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_IN:
                inst = 0x000e4f03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01eea023;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                break;
            case TK_OUT:
                inst = 0x000eaf03;
                fwrite(&inst, sizeof(uint32_t), 1, outfile);
                inst = 0x01ee0023;
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
