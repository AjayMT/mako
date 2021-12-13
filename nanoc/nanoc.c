
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "elf.h"

FILE *input = NULL;

typedef enum {
  INT_LITERAL, SEMICOLON, EOF_, LPAREN, RPAREN, LCURLY, RCURLY,
  COMMA, LT, GT, LTE, GTE, EQ, EQUAL, NEQUAL, NOT, BIT_AND, BIT_OR,
  BIT_NOT, BIT_XOR, AND, OR, PLUS, MINUS, ASTERISK, SLASH, PERCENT,
  PLUS_EQ, MINUS_EQ, ASTERISK_EQ, SLASH_EQ, PERCENT_EQ, PLUS_PLUS, MINUS_MINUS,
  AND_EQ, OR_EQ, XOR_EQ, NOT_EQ,
  CHAR_LITERAL, STR_LITERAL, IDENT,
  INT, VOID, CHAR, IF, ELSE, WHILE, CONTINUE, BREAK, RETURN
} token_type_t;

typedef struct token_s {
  token_type_t type;
  char *str;
  uint32_t pos;
} token_t;

char escape_char(char c)
{
  switch (c) {
  case 'n': return '\n';
  case 't': return '\t';
  }
  return c;
}

uint8_t buffered_token = 0;
token_t tok_buf;

token_t next_token()
{
  if (buffered_token) {
    buffered_token = 0;
    return tok_buf;
  }

  char buf[512]; // max token size is 512
  memset(buf, 0, 512);
  uint32_t buf_idx = 0;
  char c = getc(input);

  while (isspace(c)) c = getc(input); // skip spaces

#define RET_EMPTY_TOKEN(t)                                              \
  return (token_t) { .type = (t), .str = NULL, .pos = ftell(input) }

  switch (c) {
  case EOF: RET_EMPTY_TOKEN(EOF_);
  case ';': RET_EMPTY_TOKEN(SEMICOLON);
  case '(': RET_EMPTY_TOKEN(LPAREN);
  case ')': RET_EMPTY_TOKEN(RPAREN);
  case '{': RET_EMPTY_TOKEN(LCURLY);
  case '}': RET_EMPTY_TOKEN(RCURLY);
  case ',': RET_EMPTY_TOKEN(COMMA);
  }

  if (c == '<') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(LTE);
    ungetc(c, input); RET_EMPTY_TOKEN(LT);
  }

  if (c == '>') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(GTE);
    ungetc(c, input); RET_EMPTY_TOKEN(GT);
  }

  if (c == '=') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(EQUAL);
    ungetc(c, input); RET_EMPTY_TOKEN(EQ);
  }

  if (c == '!') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(NEQUAL);
    ungetc(c, input); RET_EMPTY_TOKEN(NOT);
  }

  if (c == '&') {
    c = getc(input);
    if (c == '&') RET_EMPTY_TOKEN(AND);
    if (c == '=') RET_EMPTY_TOKEN(AND_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(BIT_AND);
  }

  if (c == '|') {
    c = getc(input);
    if (c == '|') RET_EMPTY_TOKEN(OR);
    if (c == '=') RET_EMPTY_TOKEN(OR_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(BIT_OR);
  }

  if (c == '^') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(XOR_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(BIT_XOR);
  }

  if (c == '~') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(NOT_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(BIT_NOT);
  }

  if (c == '+') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(PLUS_EQ);
    if (c == '+') RET_EMPTY_TOKEN(PLUS_PLUS);
    ungetc(c, input); RET_EMPTY_TOKEN(PLUS);
  }

  if (c == '-') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(MINUS_EQ);
    if (c == '-') RET_EMPTY_TOKEN(MINUS_MINUS);
    ungetc(c, input); RET_EMPTY_TOKEN(MINUS);
  }

  if (c == '*') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(ASTERISK_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(ASTERISK);
  }

  if (c == '/') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(SLASH_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(SLASH);
  }

  if (c == '%') {
    c = getc(input);
    if (c == '=') RET_EMPTY_TOKEN(PERCENT_EQ);
    ungetc(c, input); RET_EMPTY_TOKEN(PERCENT);
  }

  if (c == '\'') {
    c = getc(input);
    uint8_t escaped = 0;
    if (c == '\\') {
      escaped = 1;
      c = getc(input);
    }
    if (c == '\'' && !escaped) goto fail;
    if (escaped) c = escape_char(c);
    buf[buf_idx] = c; ++buf_idx;
    c = getc(input);
    if (c != '\'') goto fail;
    char *s = strndup(buf, buf_idx);
    return (token_t) { .type = CHAR_LITERAL, .str = s, .pos = ftell(input) };
  }

  if (c == '"') {
    c = getc(input);
    uint8_t escaped = 0;
    while (c != '"' || escaped) {
      if (c == '\n' || c == '\r') goto fail;
      if (escaped) c = escape_char(c);
      // TODO: handle "\\"
      escaped = 0;
      if (c == '\\') escaped = 1;
      else { buf[buf_idx] = c; ++buf_idx; }
      c = getc(input);
    }
    char *s = strndup(buf, buf_idx);
    return (token_t) { .type = STR_LITERAL, .str = s, .pos = ftell(input) };
  }

  if (isdigit(c)) {
    while (isdigit(c)) {
      buf[buf_idx] = c; ++buf_idx;
      c = getc(input);
    }
    ungetc(c, input);
    char *s = strndup(buf, buf_idx);
    return (token_t) { .type = INT_LITERAL, .str = s, .pos = ftell(input) };
  }

  if (isalpha(c) || c == '_') {
    while (isalnum(c) || c == '_') {
      buf[buf_idx] = c; ++buf_idx;
      c = getc(input);
    }
    ungetc(c, input);

    if (strcmp(buf, "int") == 0) RET_EMPTY_TOKEN(INT);
    if (strcmp(buf, "char") == 0) RET_EMPTY_TOKEN(CHAR);
    if (strcmp(buf, "void") == 0) RET_EMPTY_TOKEN(VOID);
    if (strcmp(buf, "if") == 0) RET_EMPTY_TOKEN(IF);
    if (strcmp(buf, "else") == 0) RET_EMPTY_TOKEN(ELSE);
    if (strcmp(buf, "while") == 0) RET_EMPTY_TOKEN(WHILE);
    if (strcmp(buf, "continue") == 0) RET_EMPTY_TOKEN(CONTINUE);
    if (strcmp(buf, "break") == 0) RET_EMPTY_TOKEN(BREAK);
    if (strcmp(buf, "return") == 0) RET_EMPTY_TOKEN(RETURN);

    char *s = strndup(buf, buf_idx);
    return (token_t) { .type = IDENT, .str = s, .pos = ftell(input) };
  }

fail:
  printf("Malformed token: %s\nAt position %lu\n", buf, ftell(input));
  exit(1);

  return (token_t) { }; // to suppress compiler warning
}

void buffer_token(token_t tok)
{
  buffered_token = 1;
  tok_buf = tok;
}

typedef enum {
  nFUNCTION, nARGUMENT, nSTMT, nEXPR, nTYPE
} ast_node_type_t;

typedef enum {
  // nSTMT variants
  vDECL, vEXPR, vEMPTY, vBLOCK, vIF, vWHILE, vCONTINUE, vBREAK, vRETURN,

  // nEXPR variants
  vIDENT, vINT_LITERAL, vCHAR_LITERAL, vSTRING_LITERAL,
  vDEREF, vADDRESSOF, vINCREMENT, vDECREMENT, vNOT,
  vADD, vSUBTRACT, vMULTIPLY, vDIVIDE, vMODULO,
  vLT, vGT, vEQUAL, vAND, vOR,
  vBIT_AND, vBIT_OR, vBIT_XOR, vBIT_NOT,
  vASSIGN, vCALL,

  // nTYPE variants
  vINT, vCHAR, vVOID, vPTR
} ast_node_variant_t;

typedef struct ast_node_s {
  ast_node_type_t type;
  ast_node_variant_t variant;
  int32_t i;
  char *s;
  struct ast_node_s *children;
  struct ast_node_s *next;
} ast_node_t;

ast_node_t *parse_type()
{
  token_t tok = next_token();
  if (tok.type != INT && tok.type != CHAR && tok.type != VOID) {
    printf("Expected type at position %u\n", tok.pos);
    exit(1);
  }

  ast_node_t *root = malloc(sizeof(ast_node_t));
  memset(root, 0, sizeof(ast_node_t));
  root->type = nTYPE;
  if (tok.type == INT) root->variant = vINT;
  else if (tok.type == CHAR) root->variant = vCHAR;
  else root->variant = vVOID;

  tok = next_token();
  while (tok.type == ASTERISK) {
    ast_node_t *ptr_type = malloc(sizeof(ast_node_t));
    memset(ptr_type, 0, sizeof(ast_node_t));
    ptr_type->type = nTYPE;
    ptr_type->variant = vPTR;
    ptr_type->children = root;
    root = ptr_type;
    tok = next_token();
  }
  buffer_token(tok);

  return root;
}

void check_lval(ast_node_t *root, uint32_t pos)
{
  if (root == NULL) {
    printf("Invalid lvalue at position %u\n", pos);
    exit(1);
  }
  if (
    root->type != nEXPR
    || (root->variant != vIDENT && root->variant != vDEREF)
    ) {
    printf("Invalid lvalue at position %u\n", pos);
    exit(1);
  }
}

ast_node_t *parse_expr();

ast_node_t *parse_operand()
{
  ast_node_t *root;
  token_t tok = next_token();

  if (tok.type == LPAREN) {
    root = parse_expr();
    tok = next_token();
    if (tok.type != RPAREN) {
      printf("Expected ')' at position %u\n", tok.pos);
      exit(1);
    }

    goto parse_call;
  }

  root = malloc(sizeof(ast_node_t));
  memset(root, 0, sizeof(ast_node_t));
  root->type = nEXPR;

  if (tok.type == INT_LITERAL) {
    root->variant = vINT_LITERAL;
    root->i = atoi(tok.str);
    return root;
  }

  if (tok.type == CHAR_LITERAL) {
    root->variant = vCHAR_LITERAL;
    root->i = tok.str[0];
    return root;
  }

  if (tok.type == STR_LITERAL) {
    root->variant = vSTRING_LITERAL;
    root->s = tok.str;
    return root;
  }

  if (tok.type == IDENT) {
    root->variant = vIDENT;
    root->s = tok.str;
    goto parse_call;
  }

  printf("Malformed expression at position %u\n", tok.pos);
  exit(1);
  return root;

parse_call:
  tok = next_token();
  while (tok.type == LPAREN) {
    ast_node_t *callee = root;
    root = malloc(sizeof(ast_node_t));
    memset(root, 0, sizeof(ast_node_t));
    root->type = nEXPR;
    root->variant = vCALL;
    root->children = callee;

    ast_node_t **current_arg = &(callee->next);

    tok = next_token();
    while (tok.type != RPAREN) {
      buffer_token(tok);
      *current_arg = parse_expr();
      tok = next_token();
      if (tok.type == COMMA) tok = next_token();
      current_arg = &((*current_arg)->next);
    }

    tok = next_token();
  }
  buffer_token(tok);

  return root;
}

ast_node_t *parse_expr()
{
  ast_node_t *root;
  token_t tok = next_token();

  uint8_t is_unary_op = 1;
  ast_node_variant_t unary_op_variant;

  switch (tok.type) {
  case BIT_AND: unary_op_variant = vADDRESSOF; break;
  case ASTERISK: unary_op_variant = vDEREF; break;
  case BIT_NOT: unary_op_variant = vBIT_NOT; break;
  case NOT: unary_op_variant = vNOT; break;
  case PLUS_PLUS: unary_op_variant = vINCREMENT; break;
  case MINUS_MINUS: unary_op_variant = vDECREMENT; break;
  default: is_unary_op = 0;
  }

  if (is_unary_op) {
    ast_node_t *child = parse_operand();
    if (unary_op_variant == vINCREMENT || unary_op_variant == vDECREMENT)
      check_lval(child, tok.pos);
    root = malloc(sizeof(ast_node_t));
    memset(root, 0, sizeof(ast_node_t));
    root->type = nEXPR;
    root->variant = unary_op_variant;
    root->children = child;
  } else {
    buffer_token(tok);
    root = parse_operand();
  }

#define PARSE_BINOP(tok_type, variant_type, transform)  \
  if (tok.type == (tok_type)) {                         \
    ast_node_t *left = root;                            \
    ast_node_t *right = parse_operand();                \
    root = malloc(sizeof(ast_node_t));                  \
    memset(root, 0, sizeof(ast_node_t));                \
    root->type = nEXPR;                                 \
    root->variant = (variant_type);                     \
    left->next = right;                                 \
    root->children = left;                              \
    transform;                                          \
    return root;                                        \
  }                                                     \

  tok = next_token();

  PARSE_BINOP(PLUS, vADD, {});
  PARSE_BINOP(MINUS, vSUBTRACT, {});
  PARSE_BINOP(ASTERISK, vMULTIPLY, {});
  PARSE_BINOP(SLASH, vDIVIDE, {});
  PARSE_BINOP(PERCENT, vMODULO, {});
  PARSE_BINOP(LT, vLT, {});
  PARSE_BINOP(GT, vGT, {});
  PARSE_BINOP(EQUAL, vEQUAL, {});
  PARSE_BINOP(LTE, vGT, {
      ast_node_t *not_node = malloc(sizeof(ast_node_t));
      memset(not_node, 0, sizeof(ast_node_t));
      not_node->type = nEXPR;
      not_node->variant = vNOT;
      not_node->children = root;
      root = not_node;
    });
  PARSE_BINOP(GTE, vLT, {
      ast_node_t *not_node = malloc(sizeof(ast_node_t));
      memset(not_node, 0, sizeof(ast_node_t));
      not_node->type = nEXPR;
      not_node->variant = vNOT;
      not_node->children = root;
      root = not_node;
    });
  PARSE_BINOP(NEQUAL, vEQUAL, {
      ast_node_t *not_node = malloc(sizeof(ast_node_t));
      memset(not_node, 0, sizeof(ast_node_t));
      not_node->type = nEXPR;
      not_node->variant = vNOT;
      not_node->children = root;
      root = not_node;
    });
  PARSE_BINOP(AND, vAND, {});
  PARSE_BINOP(OR, vOR, {});
  PARSE_BINOP(BIT_AND, vBIT_AND, {});
  PARSE_BINOP(BIT_OR, vBIT_OR, {});
  PARSE_BINOP(BIT_XOR, vBIT_XOR, {});
  PARSE_BINOP(EQ, vASSIGN, { check_lval(left, tok.pos); });
  PARSE_BINOP(PLUS_EQ, vADD, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(MINUS_EQ, vSUBTRACT, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(ASTERISK_EQ, vMULTIPLY, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(SLASH_EQ, vDIVIDE, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(PERCENT_EQ, vMODULO, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(AND_EQ, vBIT_AND, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(OR_EQ, vBIT_OR, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(XOR_EQ, vBIT_XOR, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });
  PARSE_BINOP(NOT_EQ, vBIT_NOT, {
      check_lval(left, tok.pos);
      ast_node_t *new_left = malloc(sizeof(ast_node_t));
      memcpy(new_left, left, sizeof(ast_node_t));
      new_left->next = root;
      ast_node_t *new_root = malloc(sizeof(ast_node_t));
      memset(new_root, 0, sizeof(ast_node_t));
      new_root->type = nEXPR;
      new_root->variant = vASSIGN;
      new_root->children = new_left;
      root = new_root;
    });

  buffer_token(tok);

  return root;
}

ast_node_t *parse_stmt()
{
  ast_node_t *root = malloc(sizeof(ast_node_t));
  memset(root, 0, sizeof(ast_node_t));
  root->type = nSTMT;
  token_t tok = next_token();

  if (tok.type == SEMICOLON) {
    root->variant = vEMPTY;
    return root;
  }

  if (tok.type == CONTINUE) {
    root->variant = vCONTINUE;
    tok = next_token();
    if (tok.type != SEMICOLON) goto fail;
    return root;
  }
  if (tok.type == BREAK) {
    root->variant = vBREAK;
    tok = next_token();
    if (tok.type != SEMICOLON) goto fail;
    return root;
  }

  if (tok.type == IF) {
    root->variant = vIF;
    tok = next_token();
    if (tok.type != LPAREN) goto fail;
    ast_node_t *cond_expr = parse_expr();
    tok = next_token();
    if (tok.type != RPAREN) goto fail;
    ast_node_t *if_stmt = parse_stmt();

    // if the statement is not a block, wrap it in a block
    // simplifies symtab construction and codegen
    if (if_stmt->variant != vBLOCK) {
      ast_node_t *block = malloc(sizeof(ast_node_t));
      memset(block, 0, sizeof(ast_node_t));
      block->type = nSTMT;
      block->variant = vBLOCK;
      block->children = if_stmt;
      if_stmt = block;
    }

    tok = next_token();
    ast_node_t *else_stmt;
    if (tok.type == ELSE) else_stmt = parse_stmt();
    else {
      buffer_token(tok);
      else_stmt = malloc(sizeof(ast_node_t));
      memset(else_stmt, 0, sizeof(ast_node_t));
      else_stmt->type = nSTMT;
      else_stmt->variant = vBLOCK;
    }

    if (else_stmt->variant != vBLOCK) {
      ast_node_t *block = malloc(sizeof(ast_node_t));
      memset(block, 0, sizeof(ast_node_t));
      block->type = nSTMT;
      block->variant = vBLOCK;
      block->children = else_stmt;
      else_stmt = block;
    }

    cond_expr->next = if_stmt;
    if_stmt->next = else_stmt;
    root->children = cond_expr;
    return root;
  }

  if (tok.type == WHILE) {
    root->variant = vWHILE;
    tok = next_token();
    if (tok.type != LPAREN) goto fail;
    ast_node_t *cond_expr = parse_expr();
    tok = next_token();
    if (tok.type != RPAREN) goto fail;
    ast_node_t *while_stmt = parse_stmt();

    if (while_stmt->variant != vBLOCK) {
      ast_node_t *block = malloc(sizeof(ast_node_t));
      memset(block, 0, sizeof(ast_node_t));
      block->type = nSTMT;
      block->variant = vBLOCK;
      block->children = while_stmt;
      while_stmt = block;
    }

    cond_expr->next = while_stmt;
    root->children = cond_expr;
    return root;
  }

  if (tok.type == RETURN) {
    root->variant = vRETURN;
    tok = next_token();
    if (tok.type == SEMICOLON) return root;
    buffer_token(tok);
    ast_node_t *return_expr = parse_expr();
    root->children = return_expr;
    tok = next_token();
    if (tok.type != SEMICOLON) goto fail;
    return root;
  }

  if (tok.type == LCURLY) {
    root->variant = vBLOCK;
    ast_node_t **child = &(root->children);
    tok = next_token();
    while (tok.type != RCURLY) {
      buffer_token(tok);
      *child = parse_stmt();
      child = &((*child)->next);
      tok = next_token();
    }

    return root;
  }

  if (tok.type == INT || tok.type == CHAR || tok.type == VOID) {
    buffer_token(tok);
    root->variant = vDECL;
    ast_node_t *type_node = parse_type();
    tok = next_token();
    if (tok.type != IDENT) goto fail;
    root->s = tok.str;
    root->children = type_node;
    tok = next_token();
    if (tok.type != SEMICOLON) goto fail;
    return root;
  }

  buffer_token(tok);
  root->variant = vEXPR;
  ast_node_t *expr_node = parse_expr();
  root->children = expr_node;
  tok = next_token();
  if (tok.type != SEMICOLON) {
  fail:
    printf("Malformed statement at position %u\n", tok.pos);
    exit(1);
  }

  return root;
}

ast_node_t *parse()
{
  ast_node_t *root;
  ast_node_t **current = &root;
  token_t tok = next_token();

  while (tok.type != EOF_) {
    buffer_token(tok);
    ast_node_t *type_node = parse_type();
    tok = next_token();
    if (tok.type != IDENT) {
    fail:
      printf("Malformed declaration at position %u\n", tok.pos);
      exit(1);
    }
    char *name = tok.str;
    tok = next_token();
    if (tok.type == SEMICOLON) {
      *current = malloc(sizeof(ast_node_t));
      memset(*current, 0, sizeof(ast_node_t));
      (*current)->type = nSTMT;
      (*current)->variant = vDECL;
      (*current)->s = name;
      (*current)->children = type_node;
      current = &((*current)->next);
      tok = next_token();
      continue;
    }

    if (tok.type != LPAREN) goto fail;

    *current = malloc(sizeof(ast_node_t));
    memset(*current, 0, sizeof(ast_node_t));
    (*current)->type = nFUNCTION;
    (*current)->s = name;
    (*current)->children = type_node;

    ast_node_t **current_arg = &(type_node->next);

    tok = next_token();
    while (tok.type != RPAREN) {
      buffer_token(tok);
      ast_node_t *arg_type_node = parse_type();
      tok = next_token();
      if (tok.type != IDENT) goto fail;
      *current_arg = malloc(sizeof(ast_node_t));
      memset(*current_arg, 0, sizeof(ast_node_t));
      (*current_arg)->type = nARGUMENT;
      (*current_arg)->s = tok.str;
      (*current_arg)->children = arg_type_node;
      current_arg = &((*current_arg)->next);

      tok = next_token();
      if (tok.type == COMMA) tok = next_token();
    }

    *current_arg = parse_stmt();
    if ((*current_arg)->variant != vBLOCK && (*current_arg)->variant != vEMPTY)
      goto fail;

    current = &((*current)->next);
    tok = next_token();
  }

  return root;
}

uint32_t hash(char *str)
{
  uint32_t h = 5381;
  int c;

  while ((c = *str++))
    h = (h << 5) + h + c;

  return h;
}

typedef enum {
  tINT, tCHAR, tVOID, tINT_PTR, tCHAR_PTR, tVOID_PTR, tPTR_PTR
} symbol_type_t;

typedef enum {
  lTEXT, lDATA, lSTACK
} loc_type_t;

typedef struct symbol_s {
  char *name;
  symbol_type_t type;
  uint32_t loc;
  loc_type_t loc_type;
  struct symbol_s *child;
  struct symbol_s *parent;
} symbol_t;

// these numbers are completely arbitrary
#define DATA_START 0x3000
#define TEXT_START 0x4000

uint8_t *text = NULL;
uint32_t text_loc = 0;
uint32_t text_cap = 0;

// arbitrary data limit
#define DATA_CAP 0x1000
uint8_t data[DATA_CAP];
uint32_t data_loc = 0;

// this is arbitrary
#define SYMTAB_SIZE 256

symbol_t *root_symtab = NULL;

uint8_t symtab_insert(symbol_t *tab, symbol_t sym)
{
  uint32_t idx = hash(sym.name) % SYMTAB_SIZE;
  uint32_t i = idx;
  do {
    if (tab[i].name == NULL) {
      tab[i] = sym;
      return 0;
    }
    if (strcmp(tab[i].name, sym.name) == 0) {
      tab[i] = sym;
      return 1;
    }
    i = (i + 1) % SYMTAB_SIZE;
  } while (i != idx);
  printf("Too many (>256) symbols\n");
  exit(1);
}

symbol_t *symtab_get(symbol_t *tab, char *name)
{
  if (tab == NULL) return NULL;
  uint32_t idx = hash(name) % SYMTAB_SIZE;
  symbol_t *parent = tab[0].parent;
  uint32_t i = idx;
  do {
    if (tab[i].name == NULL) { i = (i + 1) % SYMTAB_SIZE; continue; }
    if (strcmp(tab[i].name, name) == 0) return &(tab[i]);
    i = (i + 1) % SYMTAB_SIZE;
  } while (i != idx);

  return symtab_get(parent, name);
}

symbol_type_t symbol_type_of_node_type(ast_node_t *v)
{
  switch (v->variant) {
  case vINT: return tINT;
  case vCHAR: return tCHAR;
  case vVOID: return tVOID;
  case vPTR:
    switch (v->children->variant) {
    case vINT: return tINT_PTR;
    case vCHAR: return tCHAR_PTR;
    case vVOID: return tVOID_PTR;
    case vPTR: return tPTR_PTR;
    default: ;
    }
  default: ;
  }

  return 0;
}

uint32_t construct_symtab(
  ast_node_t *root, symbol_t *out, symbol_t *parent,
  uint32_t loc, uint32_t block_id
  )
{
  if (root->type != nSTMT && root->type != nFUNCTION) {
    printf("Failed to construct symbol table\n");
    exit(1);
  }

  if (root->type == nFUNCTION) {
    symbol_t sym;
    memset(&sym, 0, sizeof(symbol_t));
    sym.parent = parent;
    sym.name = root->s;
    ast_node_t *type_node = root->children;
    sym.type = symbol_type_of_node_type(type_node);
    sym.loc = text_loc;
    sym.loc_type = lTEXT;
    sym.child = malloc(sizeof(symbol_t) * SYMTAB_SIZE);
    memset(sym.child, 0, sizeof(symbol_t) * SYMTAB_SIZE);
    sym.child[0].parent = out;

    ast_node_t *argument_nodes = type_node->next;
    ast_node_t *current_arg = argument_nodes;
    uint32_t arg_offset = 8; // 4 bytes for return address and 4 for old ebp
    while (current_arg->type == nARGUMENT) {
      symbol_t arg_sym;
      arg_sym.name = current_arg->s;
      arg_sym.loc = arg_offset;
      arg_sym.loc_type = lSTACK;
      arg_sym.type = symbol_type_of_node_type(current_arg->children);
      arg_sym.child = NULL;
      arg_sym.parent = out;
      symtab_insert(sym.child, arg_sym);
      arg_offset += 4;
      current_arg = current_arg->next;
    }

    if (current_arg->type == nSTMT && current_arg->variant == vEMPTY)
      sym.loc = -1;

    uint32_t stack_size = construct_symtab(current_arg, sym.child, out, 0, 0);
    symtab_insert(out, sym);
    return stack_size;
  }

  if (root->variant == vDECL) {
    symbol_t sym;
    sym.name = root->s;
    sym.type = symbol_type_of_node_type(root->children);
    uint32_t size = 4;
    if (sym.type == tCHAR) size = 1;
    if (out == root_symtab) {
      sym.loc = data_loc;
      sym.loc_type = lDATA;
    } else {
      sym.loc = -(loc + size);
      sym.loc_type = lSTACK;
    }
    sym.child = NULL;
    sym.parent = parent;
    symtab_insert(out, sym);
    return size;
  }

  if (root->variant == vBLOCK) {
    symbol_t sym;
    sym.name = malloc(2);
    sym.name[0] = block_id % 256;
    sym.name[1] = 0;
    sym.child = malloc(sizeof(symbol_t) * SYMTAB_SIZE);
    memset(sym.child, 0, sizeof(symbol_t) * SYMTAB_SIZE);
    sym.child[0].parent = out;
    sym.parent = parent;
    sym.loc_type = lSTACK;

    ast_node_t *current_child = root->children;
    uint32_t size = 0;
    uint32_t bid = 0;
    while (current_child != NULL) {
      size += construct_symtab(current_child, sym.child, out, loc + size, bid);
      if (current_child->variant == vBLOCK || current_child->variant == vWHILE)
        ++bid;
      if (current_child->variant == vIF) bid += 2;
      current_child = current_child->next;
    }

    sym.loc = -(loc + size); // TODO make this the text offset of the block?
    symtab_insert(out, sym);
    return size;
  }

  if (root->variant == vWHILE)
    return construct_symtab(root->children->next, out, parent, loc, block_id);

  if (root->variant == vIF) {
    uint32_t s = construct_symtab(root->children->next, out, parent, loc, block_id);
    return s + construct_symtab(
      root->children->next->next, out, parent, loc + s, block_id + 1
      );
  }

  return 0;
}

void write_text(uint8_t *b, uint32_t n)
{
  if (text == NULL || text_loc + n >= text_cap) {
    uint32_t size = 2 * (text_loc + n);
    text = realloc(text, size);
    text_cap = size;
  }
  memcpy(text + text_loc, b, n);
  text_loc += n;
}

void write_data(uint8_t *b, uint32_t n)
{
  if (data_loc + n > DATA_CAP) {
    printf("Too much data.\n");
    exit(1);
  }
  memcpy(data + data_loc, b, n);
  data_loc += n;
}

typedef enum {
  rMOV_EAX, rOFFSET, rIMM
} relocation_type_t;

typedef struct relocation_s {
  uint32_t addr;
  char *name;
  symbol_t *symtab;
  relocation_type_t type;
  struct relocation_s *next;
} relocation_t;

relocation_t *relocs = NULL;

void add_relocation(uint32_t addr, char *name, symbol_t *symtab, relocation_type_t t)
{
  relocation_t *r = malloc(sizeof(relocation_t));
  *r = (relocation_t) {
    .addr = addr, .name = name, .symtab = symtab, .type = t, .next = relocs
  };
  relocs = r;
}

uint32_t codegen_argument(ast_node_t *arg, symbol_t *symtab);

symbol_type_t codegen_expr(ast_node_t *expr, symbol_t *symtab)
{
  if (expr->variant == vINT_LITERAL || expr->variant == vCHAR_LITERAL) {
    // for int literal:
    //   movl <imm>, %eax
    // for char literal:
    //   movb <imm>, %al

    uint8_t tmp = 0xb0;
    uint32_t size = 1;
    if (expr->variant == vINT_LITERAL) { tmp = 0xb8; size = 4; }
    write_text(&tmp, 1);
    uint32_t imm = expr->i;
    for (uint32_t i = 0; i < size; ++i) {
      uint8_t tmp = imm & 0xff;
      write_text(&tmp, 1);
      imm >>= 8;
    }
    if (expr->variant == vINT_LITERAL) return tINT;
    return tCHAR;
  }

  if (expr->variant == vIDENT) {
    symbol_t *sym = symtab_get(symtab, expr->s);
    if (sym == NULL) {
      printf("Undefined symbol %s\n", expr->s);
      exit(1);
    }
    if (sym->loc == (uint32_t) -1) {
      add_relocation(text_loc, expr->s, symtab, rMOV_EAX);
      text_loc += 5;
      goto global_ident;
    }

    if (sym->loc_type == lSTACK) {
      // movl x(%ebp), %eax
      uint8_t tmp[2] = { 0x8b, 0x85 };
      if (sym->type == tCHAR) tmp[0] = 0x8a;
      write_text(tmp, 2);
      uint32_t off = sym->loc;
      for (uint32_t i = 0; i < 4; ++i) {
        uint8_t tmp0 = off;
        write_text(&tmp0, 1);
        off >>= 8;
      }
      return sym->type;
    }

    // movl addr, %eax
    uint32_t addr = DATA_START + sym->loc;
    if (sym->loc_type == lTEXT) addr = TEXT_START + sym->loc;
    uint8_t tmp = 0xb8;
    write_text(&tmp, 1);
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t tmp = addr & 0xff;
      write_text(&tmp, 1);
      addr >>= 8;
    }

  global_ident:
    // assume all symbols in .text are function pointers
    // so do not dereference
    if (sym->loc_type == lTEXT) return sym->type;

    // for int:
    //   movl (%eax), %eax
    // for char:
    //   movb (%eax), %al
    uint8_t tmp2[2] = { 0x8b, 0 };
    if (sym->type == tCHAR) tmp2[0] = 0x8a;
    write_text(tmp2, 2);
    return sym->type;
  }

  if (expr->variant == vSTRING_LITERAL) {
    // write to data section
    uint32_t addr = DATA_START + data_loc;
    uint32_t len = strlen(expr->s);
    write_data((uint8_t *)(expr->s), len + 1);

    // movl addr, %eax
    uint8_t tmp = 0xb8;
    write_text(&tmp, 1);
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t tmp = addr & 0xff;
      write_text(&tmp, 1);
      addr >>= 8;
    }

    return tCHAR_PTR;
  }

  if (expr->variant == vDEREF) {
    // for now, we do not bother returning the correct expression type
    // for more than one level of indirection because nanoc treats all pointer
    // arithmetic as being performed on char* / int

    // for int:
    //   movl (%eax), %eax
    // for char:
    //   movb (%eax), %al
    symbol_type_t ptr_type = codegen_expr(expr->children, symtab);
    uint8_t tmp2[2] = { 0x8b, 0 };
    if (ptr_type == tCHAR_PTR) tmp2[0] = 0x8a;
    write_text(tmp2, 2);
    if (ptr_type == tCHAR_PTR) return tCHAR;
    return tINT;
  }

  if (expr->variant == vADDRESSOF) {
    ast_node_t *child = expr->children;
    if (child->variant != vDEREF && child->variant != vIDENT) {
      printf("Invalid operand for 'address of' operator\n");
      exit(1);
    }
    if (child->variant == vDEREF)
      return codegen_expr(child->children, symtab);

    symbol_t *sym = symtab_get(symtab, child->s);
    if (sym == NULL) {
      printf("Undefined symbol %s\n", expr->s);
      exit(1);
    }
    if (sym->loc == (uint32_t) -1) {
      add_relocation(text_loc, child->s, symtab, rMOV_EAX);
      text_loc += 5;
      return sym->type;
    }

    if (sym->loc_type == lSTACK) {
      // movl %ebp, %eax
      uint8_t tmp[2] = { 0x89, 0xe8 };
      write_text(tmp, 2);

      // addl offset, %eax
      tmp[0] = 0x05;
      write_text(tmp, 1);
      uint32_t off = sym->loc;
      for (uint32_t i = 0; i < 4; ++i) {
        uint8_t tmp = off & 0xff;
        write_text(&tmp, 1);
        off >>= 8;
      }
      switch (sym->type) {
      case tINT: return tINT_PTR;
      case tCHAR: return tCHAR_PTR;
      case tVOID: return tVOID_PTR;
      default: return tPTR_PTR;
      }
    }

    // movl addr, %eax
    uint32_t addr = DATA_START + sym->loc;
    if (sym->loc_type == lTEXT) addr = TEXT_START + sym->loc;
    uint8_t tmp = 0xb8;
    write_text(&tmp, 1);
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t tmp = addr & 0xff;
      write_text(&tmp, 1);
      addr >>= 8;
    }

    if (sym->loc_type == lTEXT) return sym->type;
    switch (sym->type) {
    case tINT: return tINT_PTR;
    case tCHAR: return tCHAR_PTR;
    case tVOID: return tVOID_PTR;
    default: return tPTR_PTR;
    }
  }

  if (expr->variant == vINCREMENT || expr->variant == vDECREMENT) {
    ast_node_t *lval = malloc(sizeof(ast_node_t));
    memset(lval, 0, sizeof(ast_node_t));
    lval->type = nEXPR;
    lval->variant = vADDRESSOF;
    lval->children = expr->children;
    codegen_expr(lval, symtab);

    // pushl %eax
    uint8_t tmp0 = 0x50; write_text(&tmp0, 1);

    symbol_type_t child_type = codegen_expr(expr->children, symtab);
    uint8_t tmp[2] = { 0x40, 0 };
    if (expr->variant == vINCREMENT && child_type != tCHAR)
      write_text(tmp, 1);                               // incl %eax
    else if (expr->variant == vDECREMENT && child_type != tCHAR) {
      tmp[0] = 0x48; write_text(tmp, 1);                // decl %eax
    } else if (expr->variant == vINCREMENT && child_type == tCHAR) {
      tmp[0] = 0xfe; tmp[1] = 0xc0; write_text(tmp, 2); // incb %al
    } else {
      tmp[0] = 0xfe; tmp[1] = 0xc8; write_text(tmp, 2); // decb %al
    }

    // popl %ecx
    // movl %eax, (%ecx)
    tmp0 = 0x59; write_text(&tmp0, 1);
    tmp[0] = 0x89; tmp[1] = 0x01;
    write_text(tmp, 2);
    return child_type;
  }

  if (expr->variant == vNOT) {
    symbol_type_t child_type = codegen_expr(expr->children, symtab);

    // xorl %ecx, %ecx
    // test %eax, %eax
    // sete %cl
    // movl %ecx, %eax
    uint8_t tmp[9] = { 0x31, 0xc9, 0x85, 0xc0, 0x0f, 0x94, 0xc1, 0x89, 0xc8 };
    // testb %al, %al instead of test %eax, %eax
    if (child_type == tCHAR) tmp[2] = 0x84;
    write_text(tmp, 9);
    return tCHAR;
  }

  if (expr->variant == vBIT_NOT) {
    symbol_type_t child_type = codegen_expr(expr->children, symtab);
    // notl %eax
    uint8_t tmp[2] = { 0xf7, 0xd0 };
    write_text(tmp, 2);
    return child_type;
  }

  if (
    expr->variant == vADD || expr->variant == vSUBTRACT
    || expr->variant == vMULTIPLY || expr->variant == vDIVIDE
    || expr->variant == vMODULO || expr->variant == vBIT_AND
    || expr->variant == vBIT_OR || expr->variant == vBIT_XOR
    ) {
    symbol_type_t right_type = codegen_expr(expr->children->next, symtab);
    // pushl %eax
    uint8_t tmp = 0x50; write_text(&tmp, 1);
    symbol_type_t left_type = codegen_expr(expr->children, symtab);
    // popl %ecx
    // <op> %ecx, %eax
    tmp = 0x59; write_text(&tmp, 1);
    uint8_t tmp1[3] = { 0x01, 0xc8, 0 };
    uint32_t len = 2;
    if (expr->variant == vADD) {
      if (left_type == tCHAR && right_type == tCHAR)
        tmp1[0] = 0; // addb %cl, %al instead of addl %ecx, %eax
    } else if (expr->variant == vSUBTRACT) {
      tmp1[0] = 0x29; // subl/subb %ecx, %eax
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0x28;
    } else if (expr->variant == vMULTIPLY) {
      tmp1[0] = 0x0f; tmp1[1] = 0xaf; tmp1[2] = 0xc1; // imull %ecx, %eax
      len = 3;
      // TODO figure out how to multiply bytes
    } else if (expr->variant == vDIVIDE) {
      tmp1[0] = 0xf7; tmp1[1] = 0xf9; // idivl/idivb %ecx
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0xf6;
    } else if (expr->variant == vMODULO) {
      tmp1[0] = 0xf7; tmp1[1] = 0xf9; // idivl/idivb %ecx
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0xf6;
      write_text(tmp1, 2);
      tmp1[0] = 0x89; tmp1[1] = 0xd0; // movl/movb %edx, %eax
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0x88;
    } else if (expr->variant == vBIT_AND) {
      tmp1[0] = 0x21; tmp1[1] = 0xc8; // andl/andb %ecx, %eax
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0x20;
    } else if (expr->variant == vBIT_OR) {
      tmp1[0] = 0x09; tmp1[1] = 0xc8; // orl/orb %ecx, %eax
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0x08;
    } else if (expr->variant == vBIT_XOR) {
      tmp1[0] = 0x31; tmp1[1] = 0xc8; // orl/orb %ecx, %eax
      if (left_type == tCHAR && right_type == tCHAR) tmp1[0] = 0x30;
    }
    write_text(tmp1, len);
    if (left_type == tCHAR) return right_type;
    return left_type;
  }

  if (expr->variant == vLT || expr->variant == vGT || expr->variant == vEQUAL) {
    symbol_type_t right_type = codegen_expr(expr->children->next, symtab);
    // pushl %eax
    uint8_t tmp = 0x50; write_text(&tmp, 1);
    symbol_type_t left_type = codegen_expr(expr->children, symtab);
    // popl %ecx
    // cmpl/cmpb %ecx, %eax
    // setl/setg/sete %al
    // movzbl %al, %eax
    uint8_t tmp1[9] = { 0x59, 0x39, 0xc8, 0x0f, 0x9c, 0xc0, 0x0f, 0xb6, 0xc0 };
    if (left_type == tCHAR && right_type == tCHAR) tmp1[1] = 0x38;
    if (expr->variant == vGT) tmp1[4] = 0x9f;
    else if (expr->variant == vEQUAL) tmp1[4] = 0x94;
    write_text(tmp1, 9);
    return tINT;
  }

  if (expr->variant == vAND) {
    symbol_type_t right_type = codegen_expr(expr->children->next, symtab);
    // pushl %eax
    uint8_t tmp = 0x50; write_text(&tmp, 1);
    symbol_type_t left_type = codegen_expr(expr->children, symtab);
    // popl %ecx
    // mull/mulb %ecx
    // orl %edx, %eax
    // xorl %ecx, %ecx
    // cmpl/cmpb %ecx, %eax
    // setne %al
    // movzbl %al, %eax
    uint8_t tmp1[15] = {
      0x59, 0xf7, 0xe1, 0x09, 0xd0, 0x31, 0xc9, 0x39, 0xc8,
      0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0
    };
    if (left_type == tCHAR && right_type == tCHAR) {
      tmp1[1] = 0xf7; tmp1[7] = 0x38;
    }
    write_text(tmp1, 15);
    return tINT;
  }

  if (expr->variant == vOR) {
    symbol_type_t right_type = codegen_expr(expr->children->next, symtab);
    // pushl %eax
    uint8_t tmp = 0x50; write_text(&tmp, 1);
    symbol_type_t left_type = codegen_expr(expr->children, symtab);
    // popl %ecx
    // orl/orb %ecx, %eax
    // xorl %ecx, %ecx
    // cmpl/cmpb %ecx, %ax
    // setne %al
    // movzbl %al, %eax
    uint8_t tmp1[13] = {
      0x59, 0x09, 0xc8, 0x31, 0xc9, 0x39, 0xc8,
      0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0
    };
    if (left_type == tCHAR && right_type == tCHAR) {
      tmp1[1] = 0x08; tmp1[5] = 0x38;
    }
    write_text(tmp1, 13);
    return tINT;
  }

  if (expr->variant == vASSIGN) {
    ast_node_t *lval = malloc(sizeof(ast_node_t));
    memset(lval, 0, sizeof(ast_node_t));
    lval->type = nEXPR;
    lval->variant = vADDRESSOF;
    lval->children = expr->children;
    codegen_expr(lval, symtab);

    // pushl %eax
    uint8_t tmp0 = 0x50; write_text(&tmp0, 1);

    codegen_expr(expr->children->next, symtab);

    // popl %ecx
    // movl %eax, (%ecx)
    uint8_t tmp[3] = { 0x59, 0x89, 0x01 };
    write_text(tmp, 3);
  }

  if (expr->variant == vCALL) {
    uint32_t offset = codegen_argument(expr->children->next, symtab);
    symbol_type_t callee_type = codegen_expr(expr->children, symtab);
    // calll *%eax
    uint8_t tmp[2] = { 0xff, 0xd0 };
    write_text(tmp, 2);
    // addl <offset>, %esp
    tmp[0] = 0x81; tmp[1] = 0xc4;
    write_text(tmp, 2);
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t tmp0 = offset & 0xff;
      write_text(&tmp0, 1);
      offset >>= 8;
    }
    return callee_type;
  }

  return tINT;
}

uint32_t codegen_argument(ast_node_t *arg, symbol_t *symtab)
{
  if (arg == NULL) return 0;
  uint32_t offset = codegen_argument(arg->next, symtab);
  codegen_expr(arg, symtab);

  // pushl %eax
  uint8_t tmp = 0x50;
  write_text(&tmp, 1);

  return offset + 4;
}

void codegen_stmt(
  ast_node_t *stmt, symbol_t *symtab,
  uint32_t *block_id, uint32_t *continues, uint32_t *breaks
  )
{
  if (stmt->variant == vEMPTY || stmt->variant == vDECL) return;
  if (stmt->variant == vEXPR) codegen_expr(stmt->children, symtab);

  if (stmt->variant == vBLOCK) {
    char symtab_key[2] = { (char) *block_id, 0 };
    ++(*block_id);
    symbol_t *child_symtab = symtab_get(symtab, symtab_key)->child;
    ast_node_t *child = stmt->children;
    uint32_t child_block_id = 0;
    while (child != NULL) {
      codegen_stmt(child, child_symtab, &child_block_id, continues, breaks);
      child = child->next;
    }
  }

  if (stmt->variant == vRETURN) {
    if (stmt->children != NULL) codegen_expr(stmt->children, symtab);
    // leave
    // retl
    uint8_t tmp[2] = { 0xc9, 0xc3 };
    write_text(tmp, 2);
  }

  if (stmt->variant == vCONTINUE || stmt->variant == vBREAK) {
    if (
      (stmt->variant == vCONTINUE && continues == NULL)
      || (stmt->variant == vBREAK && breaks == NULL)
      ) {
      printf("Invalid '%s'\n", stmt->variant == vCONTINUE ? "continue" : "break");
      exit(1);
    }

    uint32_t *arr = stmt->variant == vCONTINUE ? continues : breaks;
    uint32_t i = 0;
    while (arr[i]) ++i;
    arr[i] = text_loc;
    text_loc += 5;
  }

  if (stmt->variant == vIF) {
    codegen_expr(stmt->children, symtab);

    // cmpl $0, %eax
    // je else_start
    // <if block>
    // jmp else_end
    // else_start:
    // <else block>
    // else_end:

    uint8_t tmp[3] = { 0x83, 0xf8, 0x00 };
    write_text(tmp, 3);

    uint32_t je_addr = text_loc;
    text_loc += 6;
    uint32_t if_start = text_loc;
    codegen_stmt(stmt->children->next, symtab, block_id, continues, breaks);

    uint32_t jmp_addr = text_loc;
    text_loc += 5;

    uint32_t else_start = text_loc;
    codegen_stmt(stmt->children->next->next, symtab, block_id, continues, breaks);
    uint32_t else_end = text_loc;

    uint32_t je_offset = else_start - if_start;
    if ((je_offset + 4) < 256) {
      uint8_t tmp1[6] = { 0x74, (je_offset + 4) & 0xff, 0x90, 0x90, 0x90, 0x90 };
      memcpy(text + je_addr, tmp1, 6);
    } else {
      uint8_t tmp1[6] = { 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00 };
      for (uint32_t i = 2; i < 6; ++i) {
        tmp1[i] = je_offset & 0xff;
        je_offset >>= 8;
      }
      memcpy(text + je_addr, tmp1, 6);
    }

    uint32_t jmp_offset = else_end - else_start;
    if ((jmp_offset + 3) < 256) {
      uint8_t tmp2[5] = { 0xeb, (jmp_offset + 3) & 0xff, 0x90, 0x90, 0x90 };
      memcpy(text + jmp_addr, tmp2, 5);
    } else {
      uint8_t tmp2[5] = { 0xe9, 0x00, 0x00, 0x00, 0x00 };
      for (uint32_t i = 1; i < 5; ++i) {
        tmp2[i] = jmp_offset & 0xff;
        jmp_offset >>= 8;
      }
      memcpy(text + jmp_addr, tmp2, 5);
    }
  }

  if (stmt->variant == vWHILE) {
    uint32_t cond_start = text_loc;
    codegen_expr(stmt->children, symtab);
    uint8_t tmp[3] = { 0x83, 0xf8, 0x00 };
    write_text(tmp, 3);

    uint32_t je_addr = text_loc;
    text_loc += 6;
    uint32_t while_start = text_loc;

    uint32_t cs[256]; memset(cs, 0, sizeof(cs));
    uint32_t bs[256]; memset(bs, 0, sizeof(bs));
    codegen_stmt(stmt->children->next, symtab, block_id, cs, bs);

    uint32_t jmp_addr = text_loc;
    text_loc += 5;
    uint32_t while_end = text_loc;

    uint32_t je_offset = while_end - while_start;
    if ((je_offset + 4) < 256) {
      uint8_t tmp1[6] = { 0x74, (je_offset + 4) & 0xff, 0x90, 0x90, 0x90, 0x90 };
      memcpy(text + je_addr, tmp1, 6);
    } else {
      uint8_t tmp1[6] = { 0x0f, 0x84, 0x00, 0x00, 0x00, 0x00 };
      for (uint32_t i = 2; i < 6; ++i) {
        tmp1[i] = je_offset & 0xff;
        je_offset >>= 8;
      }
      memcpy(text + je_addr, tmp1, 6);
    }

    uint32_t jmp_offset = while_end - cond_start;
    if (jmp_offset - 3 < 256) {
      uint8_t tmp2[5] = { 0xeb, (256 - (jmp_offset - 3)) & 0xff, 0x90, 0x90, 0x90 };
      memcpy(text + jmp_addr, tmp2, 5);
    } else {
      uint8_t tmp2[5] = { 0xe9, 0x00, 0x00, 0x00, 0x00 };
      jmp_offset = -jmp_offset;
      for (uint32_t i = 1; i < 5; ++i) {
        tmp2[i] = jmp_offset & 0xff;
        jmp_offset >>= 8;
      }
      memcpy(text + jmp_addr, tmp2, 5);
    }

    // fill in continue statements
    uint32_t ci = 0;
    while (cs[ci]) {
      uint32_t offset = cs[ci] - cond_start;
      if (offset + 2 < 256) {
        uint8_t tmp2[5] = { 0xeb, (256 - (offset + 2)) & 0xff, 0x90, 0x90, 0x90 };
        memcpy(text + cs[ci], tmp2, 5);
      } else {
        uint8_t tmp2[5] = { 0xe9, 0x00, 0x00, 0x00, 0x00 };
        offset = -(offset + 5);
        for (uint32_t i = 1; i < 5; ++i) {
          tmp2[i] = offset & 0xff;
          offset >>= 8;
        }
        memcpy(text + cs[ci], tmp2, 5);
      }
      ++ci;
    }

    // fill in break statements
    uint32_t bi = 0;
    while (bs[bi]) {
      uint32_t offset = while_end - (bs[bi] + 5);
      if ((offset + 3) < 256) {
        uint8_t tmp2[5] = { 0xeb, (offset + 3) & 0xff, 0x90, 0x90, 0x90 };
        memcpy(text + bs[bi], tmp2, 5);
      } else {
        uint8_t tmp2[5] = { 0xe9, 0x00, 0x00, 0x00, 0x00 };
        for (uint32_t i = 1; i < 5; ++i) {
          tmp2[i] = offset & 0xff;
          offset >>= 8;
        }
        memcpy(text + bs[bi], tmp2, 5);
      }
      ++bi;
    }
  }
}

void codegen(ast_node_t *ast)
{
  ast_node_t *current = ast;
  while (current != NULL) {
    uint32_t size = construct_symtab(current, root_symtab, NULL, 0, 0);

    if (current->type == nSTMT) {
      if (data_loc + size > DATA_CAP) {
        printf("Too much data.\n");
        exit(1);
      }
      data_loc += size;
      current = current->next;
      continue;
    }

    ast_node_t *current_child = current->children;
    while (current_child->type != nSTMT) current_child = current_child->next;
    if (current_child->variant != vBLOCK) {
      current = current->next; continue;
    }

    // function preamble:
    //   pushl %ebp
    //   movl %esp, %ebp
    uint8_t tmp[3] = { 0x55, 0x89, 0xe5 };
    write_text(tmp, 3);

    // subl <stacksize>, %esp
    tmp[0] = 0x81; tmp[1] = 0xec;
    write_text(tmp, 2);
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t tmp0 = size;
      write_text(&tmp0, 1);
      size >>= 8;
    }

    symbol_t *symtab = symtab_get(root_symtab, current->s)->child;
    uint32_t block_id = 0;
    codegen_stmt(current_child, symtab, &block_id, NULL, NULL);

    // function epilogue:
    //   leave
    //   retl
    tmp[0] = 0xc9; tmp[1] = 0xc3;
    write_text(tmp, 2);

    current = current->next;
  }
}

void relocate()
{
  relocation_t *current = relocs;
  while (current != NULL) {
    symbol_t *sym = symtab_get(current->symtab, current->name);
    if (sym == NULL || sym->loc == (uint32_t) -1) {
      printf("Undefined symbol %s\n", current->name);
      exit(1);
    }

    uint32_t addr = DATA_START + sym->loc;
    if (sym->loc_type == lTEXT) addr = TEXT_START + sym->loc;

    if (current->type == rMOV_EAX) {
      uint8_t tmp[5];
      tmp[0] = 0xb8;
      for (uint32_t i = 1; i < 5; ++i) {
        tmp[i] = addr & 0xff;
        addr >>= 8;
      }
      memcpy(text + current->addr, tmp, sizeof(tmp));
    }

    if (current->type == rOFFSET || current->type == rIMM) {
      uint32_t offset = addr;
      if (current->type == rOFFSET)
        offset = addr - (current->addr + TEXT_START) - 4;
      uint8_t tmp[4];
      for (uint32_t i = 0; i < 4; ++i) {
        tmp[i] = offset & 0xff;
        offset >>= 8;
      }
      memcpy(text + current->addr, tmp, sizeof(tmp));
    }

    current = current->next;
  }
}

void write_elf(FILE *out)
{
  uint32_t entry = TEXT_START;
  symbol_t *start = symtab_get(root_symtab, "_start");
  if (start != NULL) entry = TEXT_START + start->loc;
  else printf("Cannot find entry symbol _start; defaulting to %#x\n", entry);

  Elf32_Header ehdr;
  memset(&ehdr, 0, sizeof(ehdr));
  ehdr.e_ident[0] = ELFMAG0; ehdr.e_ident[1] = ELFMAG1;
  ehdr.e_ident[2] = ELFMAG2; ehdr.e_ident[3] = ELFMAG3;
  ehdr.e_ident[4] = 1; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = 1;
  ehdr.e_type = ET_EXEC;
  ehdr.e_machine = 3;
  ehdr.e_version = 1;
  ehdr.e_phnum = 2;
  ehdr.e_phentsize = sizeof(Elf32_Phdr);
  ehdr.e_phoff = sizeof(ehdr);
  ehdr.e_ehsize = sizeof(ehdr);
  ehdr.e_entry = entry;

  Elf32_Phdr text_hdr;
  memset(&text_hdr, 0, sizeof(text_hdr));
  text_hdr.p_type = PT_LOAD;
  text_hdr.p_offset = sizeof(ehdr) + (2*sizeof(Elf32_Phdr)) + data_loc;
  text_hdr.p_vaddr = TEXT_START;
  text_hdr.p_memsz = text_loc;
  text_hdr.p_filesz = text_loc;
  text_hdr.p_flags |= PF_X;

  Elf32_Phdr data_hdr;
  memset(&data_hdr, 0, sizeof(data_hdr));
  data_hdr.p_type = PT_LOAD;
  data_hdr.p_offset = sizeof(ehdr) + (2*sizeof(Elf32_Phdr));
  data_hdr.p_vaddr = DATA_START;
  data_hdr.p_memsz = data_loc;
  data_hdr.p_filesz = data_loc;

  fwrite(&ehdr, sizeof(ehdr), 1, out);
  fwrite(&data_hdr, sizeof(data_hdr), 1, out);
  fwrite(&text_hdr, sizeof(text_hdr), 1, out);
  fwrite(data, data_loc, 1, out);
  fwrite(text, text_loc, 1, out);

#ifdef NANOC_DEBUG
  printf("text offset for objdumping: %#x\n", text_hdr.p_offset);
#endif
}

struct archive_header_s {
  char ident[16];
  char mod_time[12];
  char owner_id[6];
  char group_id[6];
  char file_mode[8];
  char file_len[10];
  char end[2];
} __attribute__((packed));
typedef struct archive_header_s archive_header_t;

void read_elf(uint8_t *buffer, uint32_t len)
{
  // TODO data section stuff
  char elfmag[7] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, 1, 1, 1 };
  if (len < 5 || strncmp((char *)buffer, elfmag, 7) != 0)
    return;

  Elf32_Header *hdr = (Elf32_Header *)buffer;
  Elf32_Shdr *shstrtab_hdr =
    (Elf32_Shdr *)(buffer + hdr->e_shoff + (hdr->e_shstrndx * hdr->e_shentsize));
  char *shstrtab = (char *)(buffer + shstrtab_hdr->sh_offset);

  Elf32_Shdr *symtab_hdr = NULL, *rel_text_hdr = NULL, *strtab_hdr = NULL;
  Elf32_Shdr *text_hdr = NULL, *data_hdr = NULL, *bss_hdr = NULL, *rodata_hdr = NULL;
  uint32_t text_idx = 0, data_idx = 0, bss_idx = 0, rodata_idx = 0, unused_idx = 0;

  for (uint32_t i = 0; i < hdr->e_shnum; ++i) {
    Elf32_Shdr *current =
      (Elf32_Shdr *)(buffer + hdr->e_shoff + (hdr->e_shentsize * i));

#define FIND_SECTION(name, hdr, idx)                        \
    if (strcmp(shstrtab + current->sh_name, (name)) == 0) { \
      (idx) = i;                                            \
      (hdr) = current; continue;                            \
    }

    FIND_SECTION(".symtab", symtab_hdr, unused_idx);
    FIND_SECTION(".rel.text", rel_text_hdr, unused_idx);
    FIND_SECTION(".strtab", strtab_hdr, unused_idx);
    FIND_SECTION(".text", text_hdr, text_idx);
    FIND_SECTION(".data", data_hdr, data_idx);
    FIND_SECTION(".bss", bss_hdr, bss_idx);
    FIND_SECTION(".rodata", rodata_hdr, rodata_idx);
  }

  uint32_t text_offset = text_loc;
  write_text(buffer + text_hdr->sh_offset, text_hdr->sh_size);

  uint32_t data_offset = data_loc;
  if (data_hdr != NULL)
    write_data(buffer + data_hdr->sh_offset, data_hdr->sh_size);
  uint32_t rodata_offset = data_loc;
  if (rodata_hdr != NULL)
    write_data(buffer + rodata_hdr->sh_offset, rodata_hdr->sh_size);
  uint32_t bss_offset = data_loc;
  if (bss_hdr != NULL) {
    if (data_loc + bss_hdr->sh_size > DATA_CAP) {
      printf("Too much data.\n");
      exit(1);
    }
    memset(data + data_loc, 0, bss_hdr->sh_size);
    data_loc += bss_hdr->sh_size;
  }

  char *strtab = (char *)(buffer + strtab_hdr->sh_offset);
  Elf32_Sym *symtab = (Elf32_Sym *)(buffer + symtab_hdr->sh_offset);
  Elf32_Sym *current = symtab;
  while ((uintptr_t)current - (uintptr_t)symtab < symtab_hdr->sh_size) {
    int32_t offset = -1;
    loc_type_t loc_type = lDATA;
    if (current->st_shndx == text_idx && text_hdr != NULL) {
      offset = text_offset; loc_type = lTEXT;
    }
    if (current->st_shndx == data_idx && data_hdr != NULL) offset = data_offset;
    if (current->st_shndx == rodata_idx && rodata_hdr != NULL) offset = rodata_offset;
    if (current->st_shndx == bss_idx && bss_hdr != NULL) offset = bss_offset;
    if (offset == -1) { ++current; continue; }
    char *name = strtab + current->st_name;
    symbol_t sym; memset(&sym, 0, sizeof(sym));
    sym.name = name;
    sym.type = tINT;
    sym.loc = offset + current->st_value;
    sym.loc_type = loc_type;
    symtab_insert(root_symtab, sym);
    ++current;
  }

  Elf32_Rel *rel = (Elf32_Rel *)(buffer + rel_text_hdr->sh_offset);
  Elf32_Rel *current_rel = rel;
  while ((uintptr_t)current_rel - (uintptr_t)rel < rel_text_hdr->sh_size) {
    Elf32_Sym *sym = symtab + ELF32_R_SYM(current_rel->r_info);
    relocation_type_t type = rOFFSET;
    if (ELF32_R_TYPE(current_rel->r_info) == R_386_32) type = rIMM;
    add_relocation(
      text_offset + current_rel->r_offset,
      strtab + sym->st_name,
      root_symtab, type
      );
    ++current_rel;
  }
}

void read_archive(char *name)
{
  FILE *archive = fopen(name, "r");
  fseek(archive, 0, SEEK_END);
  uint32_t len = ftell(archive);
  fseek(archive, 0, SEEK_SET);
  uint8_t *buffer = malloc(len);
  fread(buffer, 1, len, archive);

  if (len < 8 || strncmp((char *)buffer, "!<arch>\n", 8) != 0)
    return;

  uint32_t idx = 8;
  while (idx < len) {
    archive_header_t *header = (archive_header_t *)(buffer + idx);
    uint32_t file_len = atoi(header->file_len);
    read_elf(buffer + idx + sizeof(archive_header_t), file_len);
    idx += file_len + sizeof(archive_header_t);
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("Usage: nanoc <filename> [<archive>]\n");
    return 1;
  }

  char *filename = argv[1];
  input = fopen(filename, "r");

  root_symtab = malloc(sizeof(symbol_t) * SYMTAB_SIZE);
  memset(root_symtab, 0, sizeof(symbol_t) * SYMTAB_SIZE);

  ast_node_t *root = parse();
  codegen(root);

  if (argc > 2) read_archive(argv[2]);

  relocate();

  // temporary thing to have a non-empty data section
  if (data_loc == 0) {
    memcpy(data, "asdf", 4);
    data_loc += 4;
  }

  FILE *out = fopen("a.out", "w");
  write_elf(out);

  return 0;
}
