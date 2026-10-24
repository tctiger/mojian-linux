#include "MojianMarkdown.h"
#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "parser.h"
#include "references.h"
static pthread_once_t extensions_once=PTHREAD_ONCE_INIT;
extern cmark_syntax_extension *mojian_inline_extension(void);
static cmark_syntax_extension *mojian_inline,*mojian_references;
static void free_references(cmark_mem *mem,void *data){(void)mem;cmark_map_free(data);}
static cmark_node *retain_references(cmark_syntax_extension *self,cmark_parser *parser,cmark_node *root){(void)self;cmark_node_set_user_data(root,parser->refmap);cmark_node_set_user_data_free_func(root,free_references);parser->refmap=NULL;return root;}
static void register_extensions(void){cmark_gfm_core_extensions_ensure_registered();mojian_inline=mojian_inline_extension();mojian_references=cmark_syntax_extension_new("mojian-references");cmark_syntax_extension_set_postprocess_func(mojian_references,retain_references);}
extern void *mjcm_cmark_parse_document(const char *, size_t, int);
extern void mjcm_cmark_node_free(void *);
extern char *mjcm_cmark_render_html(void *, int);
extern void *mjcm_cmark_node_first_child(void *);
extern void *mjcm_cmark_node_next(void *);
typedef struct { int common; void *root; cmark_parser *parser; } Document;
MJDocument *mj_parse(const char *source, size_t length, int commonmark, const char *extensions) {
 Document *d = calloc(1,sizeof(Document)); d->common=commonmark;
 if(commonmark) d->root=mjcm_cmark_parse_document(source,length,0);
 else {
  pthread_once(&extensions_once,register_extensions);
  d->parser=cmark_parser_new(extensions && strstr(extensions,"footnotes") ? (CMARK_OPT_FOOTNOTES | CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE) : 0);
  if(extensions&&strstr(extensions,"footnotes"))cmark_parser_attach_syntax_extension(d->parser,mojian_inline);
  char *names=strdup(extensions ? extensions : "table,strikethrough,autolink,tagfilter,tasklist");
  char *save=NULL;
  for(char *name=strtok_r(names,",",&save);name;name=strtok_r(NULL,",",&save)) {
   cmark_syntax_extension *e=cmark_find_syntax_extension(name);
   if(e)cmark_parser_attach_syntax_extension(d->parser,e);
  }
  free(names);cmark_parser_attach_syntax_extension(d->parser,mojian_references); cmark_parser_feed(d->parser,source,length); d->root=cmark_parser_finish(d->parser);
 }
 return d;
}
void mj_document_free(MJDocument *document) {Document *d=document;if(!d)return;if(d->common)mjcm_cmark_node_free(d->root);else{cmark_node_free(d->root);cmark_parser_free(d->parser);}free(d);}
MJNode *mj_root(MJDocument *document){return ((Document*)document)->root;}
char *mj_html(MJDocument *document,int unsafe_html){Document*d=document;int options=unsafe_html?CMARK_OPT_UNSAFE:0;return d->common?mjcm_cmark_render_html(d->root,options):cmark_render_html(d->root,options,cmark_parser_get_syntax_extensions(d->parser));}
void mj_string_free(char *v){free(v);}
MJNode *mj_first_child(MJDocument *document,MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_first_child(node):cmark_node_first_child(node);}
MJNode *mj_next(MJDocument *document,MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_next(node):cmark_node_next(node);}
int mj_columns(MJDocument *document,MJNode *node){return ((Document*)document)->common?0:cmark_gfm_extensions_get_table_columns(node);}
int mj_alignment(MJDocument *document,MJNode *node,int col){if(((Document*)document)->common)return 0;uint8_t*a=cmark_gfm_extensions_get_table_alignments(node);return a?a[col]:0;}
int mj_checked(MJDocument *document,MJNode *node){return ((Document*)document)->common?0:cmark_gfm_extensions_get_tasklist_item_checked(node);}
extern const char * mjcm_cmark_node_get_type_string(void *);
const char * mj_type(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_type_string(node):cmark_node_get_type_string(node);}
extern const char * mjcm_cmark_node_get_literal(void *);
extern const char *mojian_inline_literal(cmark_node *);
const char * mj_literal(MJDocument *document, MJNode *node){if(((Document*)document)->common)return mjcm_cmark_node_get_literal(node);const char *custom=mojian_inline_literal(node);return custom?custom:cmark_node_get_literal(node);}
extern const char * mjcm_cmark_node_get_url(void *);
const char * mj_url(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_url(node):cmark_node_get_url(node);}
extern const char * mjcm_cmark_node_get_title(void *);
const char * mj_title(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_title(node):cmark_node_get_title(node);}
extern const char * mjcm_cmark_node_get_fence_info(void *);
const char * mj_fence_info(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_fence_info(node):cmark_node_get_fence_info(node);}
extern int mjcm_cmark_node_get_start_line(void *);
int mj_start_line(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_start_line(node):cmark_node_get_start_line(node);}
extern int mjcm_cmark_node_get_start_column(void *);
int mj_start_column(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_start_column(node):cmark_node_get_start_column(node);}
extern int mjcm_cmark_node_get_end_line(void *);
int mj_end_line(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_end_line(node):cmark_node_get_end_line(node);}
extern int mjcm_cmark_node_get_end_column(void *);
int mj_end_column(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_end_column(node):cmark_node_get_end_column(node);}
extern int mjcm_cmark_node_get_heading_level(void *);
int mj_heading_level(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_heading_level(node):cmark_node_get_heading_level(node);}
extern int mjcm_cmark_node_get_list_type(void *);
int mj_list_type(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_list_type(node):cmark_node_get_list_type(node);}
extern int mjcm_cmark_node_get_list_start(void *);
int mj_list_start(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_list_start(node):cmark_node_get_list_start(node);}
extern int mjcm_cmark_node_get_list_tight(void *);
int mj_list_tight(MJDocument *document, MJNode *node){return ((Document*)document)->common?mjcm_cmark_node_get_list_tight(node):cmark_node_get_list_tight(node);}
#include "parser.h"
#include "references.h"
void *mj_first_reference(MJDocument *document){Document *d=document;cmark_map *map=d->common?NULL:cmark_node_get_user_data(d->root);return map?map->refs:NULL;}
void *mj_next_reference(void *reference){return ((cmark_map_entry *)reference)->next;}
const char *mj_reference_label(void *reference){return (const char *)((cmark_map_entry *)reference)->label;}
const char *mj_reference_url(MJDocument *document,void *reference){Document *d=document;return cmark_chunk_to_cstr(d->parser->mem,&((cmark_reference *)reference)->url);}
const char *mj_reference_title(MJDocument *document,void *reference){Document *d=document;return cmark_chunk_to_cstr(d->parser->mem,&((cmark_reference *)reference)->title);}
