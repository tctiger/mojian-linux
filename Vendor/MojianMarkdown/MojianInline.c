/* Source-preserving inline extensions used only by extended Markdown mode. */
#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"
#include "parser.h"
#include "inlines.h"
#include "node.h"
#include "render.h"
#include "houdini.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static cmark_node_type container_type;
static int contains(cmark_syntax_extension *self,cmark_node *node){(void)self;return node->type==container_type;}
static int can_contain(cmark_syntax_extension *self,cmark_node *node,cmark_node_type child){(void)self;return node->type==container_type&&CMARK_NODE_TYPE_INLINE_P(child);}
const char *mojian_inline_literal(cmark_node *node){return node->type==container_type?(const char *)node->content.ptr:NULL;}
static const char *type_name(cmark_syntax_extension *self,cmark_node *node){
  (void)self;switch((intptr_t)cmark_node_get_user_data(node)){case 1:return "math_inline";case 2:return "mark_inline";case 3:return "sup_inline";case 4:return "sub_inline";default:return "code";}
}
static cmark_node *match(cmark_syntax_extension *self,cmark_parser *parser,cmark_node *parent,unsigned char ch,cmark_inline_parser *in){
  (void)parent;int offset=cmark_inline_parser_get_offset(in);cmark_chunk *chunk=cmark_inline_parser_get_chunk(in);int length=chunk->len;unsigned char *data=chunk->data;int delimiter=ch=='='?2:1;
  if(ch!='$'&&ch!='='&&ch!='^'&&ch!='~')return NULL;
  if(offset>0&&data[offset-1]==ch)return NULL;
  if(ch=='^'&&offset>0&&data[offset-1]=='[')return NULL;
  if(offset+delimiter>=length)return NULL;
  if(ch=='='&&(data[offset+1]!='='||(offset+2<length&&data[offset+2]=='=')))return NULL;
  if(ch!='='&&data[offset+1]==ch)return NULL;
  int end=offset+delimiter;
  for(;end<length&&end-offset<16000;++end){
    if(data[end]=='\n'||data[end]=='\r')return NULL;
    if(data[end]=='\\'){if(end+1<length){if(data[end+1]=='\n'||data[end+1]=='\r')return NULL;++end;}continue;}
    if(data[end]!=ch)continue;
    if(delimiter==2&&(end+1>=length||data[end+1]!=ch))continue;
    if(end+delimiter<length&&data[end+delimiter]==ch)return NULL;
    break;
  }
  if(end>=length||end==offset+delimiter||end-offset>=16000)return NULL;
  size_t count=(size_t)(end-offset-delimiter);char *body=malloc(count+1);if(!body)return NULL;memcpy(body,data+offset+delimiter,count);body[count]=0;
  int depth=0;for(cmark_node *ancestor=parent;ancestor;ancestor=ancestor->parent)if(++depth>256){free(body);return NULL;}
  cmark_node *node=cmark_node_new_with_mem_and_ext(ch=='$'?CMARK_NODE_CODE:container_type,parser->mem,self);
  if(ch=='$')cmark_node_set_literal(node,body);else{cmark_strbuf_puts(&node->content,body);node->internal_offset=delimiter;}free(body);cmark_node_set_user_data(node,(void *)(intptr_t)(ch=='$'?1:ch=='='?2:ch=='^'?3:4));
  node->start_line=node->end_line=cmark_inline_parser_get_line(in);node->start_column=cmark_inline_parser_get_column(in);node->end_column=node->start_column+(end-offset)+delimiter-1;
  if(ch!='$'){node->parent=parent;cmark_parse_inlines(parser,node,parser->refmap,parser->options);node->parent=NULL;}
  cmark_inline_parser_set_offset(in,end+delimiter);return node;
}
static void html(cmark_syntax_extension *self,struct cmark_html_renderer *renderer,cmark_node *node,cmark_event_type event,int options){
  (void)self;(void)options;intptr_t type=(intptr_t)cmark_node_get_user_data(node);const char *delimiter=type==1?"$":type==2?"==":type==3?"^":"~";
  if(node->type==container_type){cmark_strbuf_puts(renderer->html,delimiter);return;}if(event!=CMARK_EVENT_ENTER)return;cmark_strbuf_puts(renderer->html,delimiter);houdini_escape_html(renderer->html,node->as.literal.data,node->as.literal.len);cmark_strbuf_puts(renderer->html,delimiter);
}
cmark_syntax_extension *mojian_inline_extension(void){
  container_type=cmark_syntax_extension_add_node(1);cmark_syntax_extension *ext=cmark_syntax_extension_new("mojian-inline");cmark_syntax_extension_set_can_contain_func(ext,can_contain);cmark_syntax_extension_set_contains_inlines_func(ext,contains);cmark_syntax_extension_set_match_inline_func(ext,match);cmark_syntax_extension_set_get_type_string_func(ext,type_name);cmark_syntax_extension_set_html_render_func(ext,html);
  cmark_llist *chars=NULL;cmark_mem *mem=cmark_get_default_mem_allocator();for(const char *p="$=^~";*p;++p)chars=cmark_llist_append(mem,chars,(void *)(intptr_t)*p);cmark_syntax_extension_set_special_inline_chars(ext,chars);return ext;
}
