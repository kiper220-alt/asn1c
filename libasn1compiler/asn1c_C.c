/*
 * Don't look into this file. First, because it's a mess, and second, because
 * it's a brain of the compiler, and you don't wanna mess with brains do you? ;)
 */
#include "asn1c_internal.h"
#include "asn1c_C.h"
#include "asn1c_constraint.h"
#include <asn1fix_export.h>	/* Stuff exported by libasn1fix */

typedef struct tag2el_s {
	struct asn1p_type_tag_s el_tag;
	int el_no;
	int toff_first;
	int toff_last;
	asn1p_expr_t *from_expr;
} tag2el_t;

static int _fill_tag2el_map(arg_t *arg, tag2el_t **tag2el, int *count, int el_no);
static int _add_tag2el_member(arg_t *arg, tag2el_t **tag2el, int *count, int el_no);

static int asn1c_lang_C_type_SEQUENCE_def(arg_t *arg);
static int asn1c_lang_C_type_SET_def(arg_t *arg);
static int asn1c_lang_C_type_CHOICE_def(arg_t *arg);
static int asn1c_lang_C_type_SEx_OF_def(arg_t *arg, int seq_of);
static int _print_tag(arg_t *arg, asn1p_expr_t *expr, struct asn1p_type_tag_s *tag_p);
static int check_if_extensible(asn1p_expr_t *expr);
static int emit_tags_vector(arg_t *arg, asn1p_expr_t *expr, int *tags_impl_skip, int choice_mode);
static int emit_tag2member_map(arg_t *arg, tag2el_t *tag2el, int tag2el_count);

#define	C99_MODE	(arg->flags & A1C_NO_C99)
#define	UNNAMED_UNIONS	(arg->flags & A1C_UNNAMED_UNIONS)

#define	PCTX_DEF INDENTED(		\
	OUT("\n");			\
	OUT("/* Context for parsing across buffer boundaries */\n");	\
	OUT("ber_dec_ctx_t _ber_dec_ctx;\n"));

#define	DEPENDENCIES	do {						\
	TQ_FOR(v, &(expr->members), next) {				\
		if((!(v->expr_type & ASN_CONSTR_MASK)			\
		&& v->expr_type > ASN_CONSTR_MASK)			\
		|| v->meta_type == AMT_TYPEREF) {			\
			GEN_INCLUDE(asn1c_type_name(arg, v, TNF_INCLUDE));\
		}							\
	}								\
	if(expr->expr_type == ASN_CONSTR_SET_OF)			\
		GEN_INCLUDE("asn_SET_OF");				\
	if(expr->expr_type == ASN_CONSTR_SEQUENCE_OF)			\
		GEN_INCLUDE("asn_SEQUENCE_OF");				\
} while(0)

#define	MKID(id)	asn1c_make_identifier(0, (id), 0)

int
asn1c_lang_C_type_ENUMERATED(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;

	REDIR(OT_DEPS);

	OUT("typedef enum %s {\n", MKID(expr->Identifier));
	TQ_FOR(v, &(expr->members), next) {
		switch(v->expr_type) {
		case A1TC_UNIVERVAL:
			OUT("\t%s\t= %lld,\n",
				asn1c_make_identifier(0,
					expr->Identifier,
					v->Identifier, 0),
				v->value->value.v_integer);
			break;
		case A1TC_EXTENSIBLE:
			OUT("\t/*\n");
			OUT("\t * Enumeration is extensible\n");
			OUT("\t */\n");
			break;
		default:
			return -1;
		}
	}
	OUT("} %s_e;\n", MKID(expr->Identifier));

	return asn1c_lang_C_type_SIMPLE_TYPE(arg);
}


int
asn1c_lang_C_type_INTEGER(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;

	REDIR(OT_DEPS);

	if(TQ_FIRST(&(expr->members))) {
		OUT("typedef enum %s {\n", MKID(expr->Identifier));
		TQ_FOR(v, &(expr->members), next) {
			switch(v->expr_type) {
			case A1TC_UNIVERVAL:
				OUT("\t%s\t= %lld,\n",
					asn1c_make_identifier(0,
						expr->Identifier,
						v->Identifier, 0),
					v->value->value.v_integer);
				break;
			default:
				return -1;
			}
		}
		OUT("} %s_e;\n", MKID(expr->Identifier));
	}

	return asn1c_lang_C_type_SIMPLE_TYPE(arg);
}

int
asn1c_lang_C_type_SEQUENCE(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	int comp_mode = 0;	/* {root,ext=1,root,root,...} */

	DEPENDENCIES;

	if(arg->embed) {
		OUT("struct %s {\n",
			MKID(expr->Identifier));
	} else {
		OUT("typedef struct %s {\n",
			MKID(expr->Identifier));
	}

	TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE) {
			if(comp_mode < 3) comp_mode++;
		}
		if(comp_mode == 1 && !v->marker)
			v->marker = EM_OPTIONAL;
		EMBED(v);
	}

	PCTX_DEF;
	OUT("} %s%s", expr->marker?"*":"",
		MKID(expr->Identifier));
	if(arg->embed) OUT(";\n"); else OUT("_t;\n");

	return asn1c_lang_C_type_SEQUENCE_def(arg);
}

static int
asn1c_lang_C_type_SEQUENCE_def(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	int elements;	/* Number of elements */
	int tags_impl_skip = 0;
	int comp_mode = 0;	/* {root,ext=1,root,root,...} */
	int ext_start = -1;
	int ext_stop = -1;
	tag2el_t *tag2el = NULL;
	int tag2el_count = 0;
	int tags_count;
	char *p;

	/*
	 * Fetch every inner tag from the tag to elements map.
	 */
	if(_fill_tag2el_map(arg, &tag2el, &tag2el_count, -1)) {
		if(tag2el) free(tag2el);
		return -1;
	}

	GEN_INCLUDE("constr_SEQUENCE");
	if(!arg->embed)
		GEN_DECLARE(expr);	/* asn1_DEF_xxx */

	REDIR(OT_STAT_DEFS);

	/*
	 * Print out the table according to which the parsing is performed.
	 */
	p = MKID(expr->Identifier);
	OUT("static asn1_SEQUENCE_element_t asn1_DEF_%s_elements[] = {\n", p);

	elements = 0;
	INDENTED(TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE) {
			if((++comp_mode) == 1)
				ext_start = elements - 1;
			else
				ext_stop = elements - 1;
			continue;
		}
	OUT("{ ");
		elements++;
		OUT("offsetof(struct %s, ", MKID(expr->Identifier));
		OUT("%s), ", MKID(v->Identifier));
		if(v->marker) {
			asn1p_expr_t *tv;
			int opts = 0;
			for(tv = v; tv && tv->marker;
				tv = TQ_NEXT(tv, next), opts++) {
				if(tv->expr_type == A1TC_EXTENSIBLE)
					opts--;
			}
			OUT("%d,", opts);
		} else {
			OUT("0,");
		}
		OUT("\n");
		INDENT(+1);
		if(C99_MODE) OUT(".tag = ");
		_print_tag(arg, v, NULL);
		OUT(",\n");
		if(C99_MODE) OUT(".tag_mode = ");
		if(v->tag.tag_class) {
			if(v->tag.tag_mode == TM_IMPLICIT)
			OUT("-1,\t/* IMPLICIT tag at current level */\n");
			else
			OUT("+1,\t/* EXPLICIT tag at current level */\n");
		} else {
			OUT("0,\n");
		}
		if(C99_MODE) OUT(".type = ");
		OUT("(void *)&asn1_DEF_%s,\n",
			asn1c_type_name(arg, v, TNF_SAFE));
		if(C99_MODE) OUT(".name = ");
		OUT("\"%s\"\n", v->Identifier);
		OUT("},\n");
		INDENT(-1);
	});
	OUT("};\n");

	/*
	 * Print out asn1_DEF_<type>_tags[] vector.
	 */
	tags_count = emit_tags_vector(arg, expr, &tags_impl_skip, 0);

	/*
	 * Tags to elements map.
	 */
	emit_tag2member_map(arg, tag2el, tag2el_count);

	p = MKID(expr->Identifier);
	OUT("static asn1_SEQUENCE_specifics_t asn1_DEF_%s_specs = {\n", p);
	INDENTED(
		OUT("sizeof(struct %s),\n", p);
		OUT("offsetof(struct %s, _ber_dec_ctx),\n", p);
		OUT("asn1_DEF_%s_elements,\n", p);
		OUT("%d,\t/* Elements count */\n", elements);
		OUT("asn1_DEF_%s_tag2el,\n", p);
		OUT("%d,\t/* Count of tags in the map */\n", tag2el_count);
		OUT("%d,\t/* Start extensions */\n",
			ext_start);
		OUT("%d\t/* Stop extensions */\n",
			(ext_stop<ext_start)?elements+1:ext_stop, ext_stop);
	);
	OUT("};\n");
	OUT("asn1_TYPE_descriptor_t asn1_DEF_%s = {\n", p);
	INDENTED(
		OUT("\"%s\",\n", expr->Identifier);
		OUT("SEQUENCE_constraint,\n");
		OUT("SEQUENCE_decode_ber,\n");
		OUT("SEQUENCE_encode_der,\n");
		OUT("SEQUENCE_print,\n");
		OUT("SEQUENCE_free,\n");
		OUT("0,\t/* Use generic outmost tag fetcher */\n");
		if(tags_count) {
			OUT("asn1_DEF_%s_tags,\n", p);
			OUT("sizeof(asn1_DEF_%s_tags)\n", p);
			OUT("\t/sizeof(asn1_DEF_%s_tags[0]), /* %d */\n",
				p, tags_count);
		} else {
			OUT("0,\t/* No explicit tags (pointer) */\n");
			OUT("0,\t/* No explicit tags (count) */\n");
		}
		OUT("%d,\t/* Tags to skip */\n", tags_impl_skip);
		OUT("%d,\t/* Whether CONSTRUCTED */\n", 1);
		OUT("&asn1_DEF_%s_specs\t/* Additional specs */\n", p);
	);
	OUT("};\n");
	OUT("\n");

	REDIR(OT_TYPE_DECLS);

	return 0;
}

int
asn1c_lang_C_type_SEQUENCE_OF(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;

	DEPENDENCIES;

	if(arg->embed) {
		OUT("struct %s {\n", MKID(expr->Identifier));
	} else {
		OUT("typedef struct %s {\n", MKID(expr->Identifier));
	}

	TQ_FOR(v, &(expr->members), next) {
		INDENTED(OUT("A_SEQUENCE_OF(%s) list;\n",
			asn1c_type_name(arg, v, TNF_RSAFE)));
	}

	PCTX_DEF;
	OUT("} %s%s", expr->marker?"*":"", MKID(expr->Identifier));
	if(arg->embed) OUT(";\n"); else OUT("_t;\n");

	/*
	 * SET OF/SEQUENCE OF definition, SEQUENCE OF mode.
	 */
	return asn1c_lang_C_type_SEx_OF_def(arg, 1);
}

int
asn1c_lang_C_type_SET(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	long mcount;
	char *id;
	int comp_mode = 0;	/* {root,ext=1,root,root,...} */

	DEPENDENCIES;

	REDIR(OT_DEPS);

	OUT("\n");
	OUT("/*\n");
	OUT(" * Method of determining the components presence\n");
	OUT(" */\n");
	mcount = 0;
	OUT("typedef enum %s_PR {\n", MKID(expr->Identifier));
	TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE) continue;
		INDENTED(
			id = MKID(expr->Identifier);
			OUT("%s_PR_", id);
			id = MKID(v->Identifier);
			OUT("%s,\t/* Member %s is present */\n",
				id, id)
		);
		mcount++;
	}
	id = MKID(expr->Identifier);
	OUT("} %s_PR;\n", id);

	REDIR(OT_TYPE_DECLS);

	if(arg->embed) {
		OUT("struct %s {\n", id);
	} else {
		OUT("typedef struct %s {\n", id);
	}

	TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE) {
			if(comp_mode < 3) comp_mode++;
		}
		if(comp_mode == 1 && !v->marker)
			v->marker = EM_OPTIONAL;
		EMBED(v);
	}

	INDENTED(
		id = MKID(expr->Identifier);
		OUT("\n");
		OUT("/* Presence bitmask: ASN_SET_ISPRESENT(p%s, %s_PR_x) */\n",
			id, id);
		OUT("unsigned int _presence_map\n");
		OUT("\t[((%ld+(8*sizeof(unsigned int))-1)/(8*sizeof(unsigned int)))];\n", mcount);
	);

	PCTX_DEF;
	OUT("} %s%s", expr->marker?"*":"", MKID(expr->Identifier));
	if(arg->embed) OUT(";\n"); else OUT("_t;\n");

	return asn1c_lang_C_type_SET_def(arg);
}

static int
asn1c_lang_C_type_SET_def(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	int elements;
	int tags_impl_skip = 0;
	int comp_mode = 0;	/* {root,ext=1,root,root,...} */
	tag2el_t *tag2el = NULL;
	int tag2el_count = 0;
	int tags_count;
	char *p;

	/*
	 * Fetch every inner tag from the tag to elements map.
	 */
	if(_fill_tag2el_map(arg, &tag2el, &tag2el_count, -1)) {
		if(tag2el) free(tag2el);
		return -1;
	}

	GEN_INCLUDE("constr_SET");
	if(!arg->embed)
		GEN_DECLARE(expr);	/* asn1_DEF_xxx */

	REDIR(OT_STAT_DEFS);

	/*
	 * Print out the table according to which the parsing is performed.
	 */
	p = MKID(expr->Identifier);
	OUT("static asn1_SET_element_t asn1_DEF_%s_elements[] = {\n", p);

	elements = 0;
	INDENTED(TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type != A1TC_EXTENSIBLE) {
			if(comp_mode == 1)
				v->marker = EM_OPTIONAL;
			elements++;
		} else {
			if(comp_mode < 3) comp_mode++;
			continue;
		}
	OUT("{ ");
		p = MKID(expr->Identifier);
		OUT("offsetof(struct %s, ", p);
		p = MKID(v->Identifier);
		OUT("%s), ", p);
		if(v->marker) {
			OUT("1, /* Optional element */\n");
		} else {
			OUT("0,\n");
		}
		INDENT(+1);
		if(C99_MODE) OUT(".tag = ");
		_print_tag(arg, v, NULL);
		OUT(",\n");
		if(C99_MODE) OUT(".tag_mode = ");
		if(v->tag.tag_class) {
			if(v->tag.tag_mode == TM_IMPLICIT)
			OUT("-1,\t/* IMPLICIT tag at current level */\n");
			else
			OUT("+1,\t/* EXPLICIT tag at current level */\n");
		} else {
			OUT("0,\n");
		}
		if(C99_MODE) OUT(".type = ");
		OUT("(void *)&asn1_DEF_%s,\n",
			asn1c_type_name(arg, v, TNF_SAFE));
		if(C99_MODE) OUT(".name = ");
		OUT("\"%s\"\n", v->Identifier);
		OUT("},\n");
		INDENT(-1);
	});
	OUT("};\n");

	/*
	 * Print out asn1_DEF_<type>_tags[] vector.
	 */
	tags_count = emit_tags_vector(arg, expr, &tags_impl_skip, 0);

	/*
	 * Tags to elements map.
	 */
	emit_tag2member_map(arg, tag2el, tag2el_count);

	/*
	 * Emit a map of mandatory elements.
	 */
	p = MKID(expr->Identifier);
	OUT("static uint8_t asn1_DEF_%s_mmap", p);
	OUT("[(%d + (8 * sizeof(unsigned int)) - 1) / 8]", elements);
	OUT(" = {\n", p);
	INDENTED(
	if(elements) {
		int delimit = 0;
		int el = 0;
		TQ_FOR(v, &(expr->members), next) {
			if(v->expr_type == A1TC_EXTENSIBLE) continue;
			if(delimit) {
				OUT(",\n");
				delimit = 0;
			} else if(el) {
				OUT(" | ");
			}
			OUT("(%d << %d)", v->marker?0:1, 7 - (el % 8));
			if(el && (el % 8) == 0)
				delimit = 1;
			el++;
		}
	} else {
		OUT("0");
	}
	);
	OUT("\n");
	OUT("};\n");

	OUT("static asn1_SET_specifics_t asn1_DEF_%s_specs = {\n", p);
	INDENTED(
		OUT("sizeof(struct %s),\n", p);
		OUT("offsetof(struct %s, _ber_dec_ctx),\n", p);
		OUT("offsetof(struct %s, _presence_map),\n", p);
		OUT("asn1_DEF_%s_elements,\n", p);
		OUT("%d,\t/* Elements count */\n", elements);
		OUT("asn1_DEF_%s_tag2el,\n", p);
		OUT("%d,\t/* Count of tags in the map */\n", tag2el_count);
		OUT("%d,\t/* Whether extensible */\n",
			check_if_extensible(expr));
		OUT("(unsigned int *)asn1_DEF_%s_mmap\t/* Mandatory elements map */\n", p);
	);
	OUT("};\n");
	OUT("asn1_TYPE_descriptor_t asn1_DEF_%s = {\n", p);
	INDENTED(
		OUT("\"%s\",\n", expr->Identifier);
		OUT("SET_constraint,\n");
		OUT("SET_decode_ber,\n");
		OUT("SET_encode_der,\n");
		OUT("SET_print,\n");
		OUT("SET_free,\n");
		OUT("0,\t/* Use generic outmost tag fetcher */\n");
		if(tags_count) {
			OUT("asn1_DEF_%s_tags,\n", p);
			OUT("sizeof(asn1_DEF_%s_tags)\n", p);
			OUT("\t/sizeof(asn1_DEF_%s_tags[0]), /* %d */\n",
				p, tags_count);
		} else {
			OUT("0,\t/* No explicit tags (pointer) */\n");
			OUT("0,\t/* No explicit tags (count) */\n");
		}
		OUT("%d,\t/* Tags to skip */\n", tags_impl_skip);
		OUT("%d,\t/* Whether CONSTRUCTED */\n", 1);
		OUT("&asn1_DEF_%s_specs\t/* Additional specs */\n", p);
	);
	OUT("};\n");
	OUT("\n");

	REDIR(OT_TYPE_DECLS);

	return 0;
}

int
asn1c_lang_C_type_SET_OF(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;

	DEPENDENCIES;

	if(arg->embed) {
		OUT("struct %s {\n", MKID(expr->Identifier));
	} else {
		OUT("typedef struct %s {\n",
			MKID(expr->Identifier));
	}

	TQ_FOR(v, &(expr->members), next) {
		INDENTED(OUT("A_SET_OF(%s) list;\n",
			asn1c_type_name(arg, v, TNF_RSAFE)));
	}

	PCTX_DEF;
	OUT("} %s%s", expr->marker?"*":"", MKID(expr->Identifier));
	if(arg->embed) OUT(";\n"); else OUT("_t;\n");

	/*
	 * SET OF/SEQUENCE OF definition, SET OF mode.
	 */
	return asn1c_lang_C_type_SEx_OF_def(arg, 0);
}

static int
asn1c_lang_C_type_SEx_OF_def(arg_t *arg, int seq_of) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	int tags_impl_skip = 0;
	int tags_count;
	char *p;

	/*
	 * Print out the table according to which the parsing is performed.
	 */
	if(seq_of) {
		GEN_INCLUDE("constr_SEQUENCE_OF");
	} else {
		GEN_INCLUDE("constr_SET_OF");
	}
	if(!arg->embed)
		GEN_DECLARE(expr);	/* asn1_DEF_xxx */

	REDIR(OT_STAT_DEFS);

	/*
	 * Print out the table according to which the parsing is performed.
	 */
	p = MKID(expr->Identifier);
	OUT("static asn1_SET_OF_element_t asn1_DEF_%s_elements[] = {\n", p);

	INDENTED(OUT("{ ");
		v = TQ_FIRST(&(expr->members));
		INDENT(+1);
		if(C99_MODE) OUT(".tag = ");
		_print_tag(arg, v, NULL);
		OUT(",\n");
		if(C99_MODE) OUT(".type = ");
		OUT("(void *)&asn1_DEF_%s",
			asn1c_type_name(arg, v, TNF_SAFE));
		OUT(" ");
		OUT("},\n");
		INDENT(-1);
	);
	OUT("};\n");

	/*
	 * Print out asn1_DEF_<type>_tags[] vector.
	 */
	tags_count = emit_tags_vector(arg, expr, &tags_impl_skip, 0);

	p = MKID(expr->Identifier);
	OUT("static asn1_SET_OF_specifics_t asn1_DEF_%s_specs = {\n", p);
	INDENTED(
		OUT("sizeof(struct %s),\n", p);
		OUT("offsetof(struct %s, _ber_dec_ctx),\n", p);
		OUT("asn1_DEF_%s_elements\n", p);
	);
	OUT("};\n");
	OUT("asn1_TYPE_descriptor_t asn1_DEF_%s = {\n", p);
	INDENTED(
		OUT("\"%s\",\n", expr->Identifier);
		if(seq_of) {
			OUT("SEQUENCE_OF_constraint,\n");
			OUT("SEQUENCE_OF_decode_ber,\n");
			OUT("SEQUENCE_OF_encode_der,\n");
			OUT("SEQUENCE_OF_print,\n");
			OUT("SEQUENCE_OF_free,\n");
		} else {
			OUT("SET_OF_constraint,\n");
			OUT("SET_OF_decode_ber,\n");
			OUT("SET_OF_encode_der,\n");
			OUT("SET_OF_print,\n");
			OUT("SET_OF_free,\n");
		}
		OUT("0,\t/* Use generic outmost tag fetcher */\n");
		if(tags_count) {
			OUT("asn1_DEF_%s_tags,\n", p);
			OUT("sizeof(asn1_DEF_%s_tags)\n", p);
			OUT("\t/sizeof(asn1_DEF_%s_tags[0]), /* %d */\n",
				p, tags_count);
		} else {
			OUT("0,\t/* No explicit tags (pointer) */\n");
			OUT("0,\t/* No explicit tags (count) */\n");
		}
		OUT("%d,\t/* Tags to skip */\n", tags_impl_skip);
		OUT("%d,\t/* Whether CONSTRUCTED */\n", 1);
		OUT("&asn1_DEF_%s_specs\t/* Additional specs */\n", p);
	);
	OUT("};\n");
	OUT("\n");

	REDIR(OT_TYPE_DECLS);

	return 0;
}

int
asn1c_lang_C_type_CHOICE(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	char *p;

	DEPENDENCIES;

	REDIR(OT_DEPS);

	p = MKID(expr->Identifier);
	OUT("typedef enum %s_PR {\n", p);
	INDENTED(
		p = MKID(expr->Identifier);
		OUT("%s_PR_NOTHING,\t"
			"/* No components present */\n", p);
		TQ_FOR(v, &(expr->members), next) {
			if(v->expr_type == A1TC_EXTENSIBLE) {
				OUT("/* Extensions may appear below */\n");
				continue;
			}
			p = MKID(expr->Identifier);
			OUT("%s_PR_", p);
			p = MKID(v->Identifier);
			OUT("%s,\n", p, p);
		}
	);
	p = MKID(expr->Identifier);
	OUT("} %s_PR;\n", p);

	REDIR(OT_TYPE_DECLS);

	if(arg->embed) {
		OUT("struct %s {\n", p);
	} else {
		OUT("typedef struct %s {\n", p);
	}

	INDENTED(
		OUT("%s_PR present;\n", p);
		OUT("union {\n", p);
		TQ_FOR(v, &(expr->members), next) {
			EMBED(v);
		}
		if(UNNAMED_UNIONS)	OUT("};\n");
		else			OUT("} choice;\n");
	);

	PCTX_DEF;
	OUT("} %s%s", expr->marker?"*":"", MKID(expr->Identifier));
	if(arg->embed) OUT(";\n"); else OUT("_t;\n");

	return asn1c_lang_C_type_CHOICE_def(arg);
}

static int
asn1c_lang_C_type_CHOICE_def(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	asn1p_expr_t *v;
	int elements;	/* Number of elements */
	int tags_impl_skip = 0;
	int comp_mode = 0;	/* {root,ext=1,root,root,...} */
	tag2el_t *tag2el = NULL;
	int tag2el_count = 0;
	int tags_count;
	char *p;

	/*
	 * Fetch every inner tag from the tag to elements map.
	 */
	if(_fill_tag2el_map(arg, &tag2el, &tag2el_count, -1)) {
		if(tag2el) free(tag2el);
		return -1;
	}

	GEN_INCLUDE("constr_CHOICE");
	if(!arg->embed)
		GEN_DECLARE(expr);	/* asn1_DEF_xxx */

	REDIR(OT_STAT_DEFS);

	/*
	 * Print out the table according to which the parsing is performed.
	 */
	p = MKID(expr->Identifier);
	OUT("static asn1_CHOICE_element_t asn1_DEF_%s_elements[] = {\n", p);

	elements = 0;
	INDENTED(TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type != A1TC_EXTENSIBLE) {
			if(comp_mode == 1)
				v->marker = EM_OPTIONAL;
			elements++;
		} else {
			if(comp_mode < 3) comp_mode++;
			continue;
		}
	OUT("{ ");
		p = MKID(expr->Identifier);
		OUT("offsetof(struct %s, ", p);
		p = MKID(v->Identifier);
		if(!UNNAMED_UNIONS) OUT("choice.");
		OUT("%s), ", p);
		if(v->marker) {
			OUT("1, /* Optional element */\n");
		} else {
			OUT("0,\n");
		}
		INDENT(+1);
		if(C99_MODE) OUT(".tag = ");
		_print_tag(arg, v, NULL);
		OUT(",\n");
		if(C99_MODE) OUT(".tag_mode = ");
		if(v->tag.tag_class) {
			if(v->tag.tag_mode == TM_IMPLICIT)
			OUT("-1,\t/* IMPLICIT tag at current level */\n");
			else
			OUT("+1,\t/* EXPLICIT tag at current level */\n");
		} else {
			OUT("0,\n");
		}
		if(C99_MODE) OUT(".type = ");
		OUT("(void *)&asn1_DEF_%s,\n",
			asn1c_type_name(arg, v, TNF_SAFE));
		if(C99_MODE) OUT(".name = ");
		OUT("\"%s\"\n", v->Identifier);
		OUT("},\n");
		INDENT(-1);
	});
	OUT("};\n");


	if(arg->embed) {
		/*
		 * Our parent structure has already taken this into account.
		 */
		tags_count = 0;
	} else {
		tags_count = emit_tags_vector(arg, expr, &tags_impl_skip, 1);
	}

	/*
	 * Tags to elements map.
	 */
	emit_tag2member_map(arg, tag2el, tag2el_count);

	p = MKID(expr->Identifier);
	OUT("static asn1_CHOICE_specifics_t asn1_DEF_%s_specs = {\n", p);
	INDENTED(
		OUT("sizeof(struct %s),\n", p);
		OUT("offsetof(struct %s, _ber_dec_ctx),\n", p);
		OUT("offsetof(struct %s, present),\n", p);
		OUT("sizeof(((struct %s *)0)->present),\n", p);
		OUT("asn1_DEF_%s_elements,\n", p);
		OUT("%d,\t/* Elements count */\n", elements);
		OUT("asn1_DEF_%s_tag2el,\n", p);
		OUT("%d,\t/* Count of tags in the map */\n", tag2el_count);
		OUT("%d\t/* Whether extensible */\n",
			check_if_extensible(expr));
	);
	OUT("};\n");
	OUT("asn1_TYPE_descriptor_t asn1_DEF_%s = {\n", p);
	INDENTED(
		OUT("\"%s\",\n", expr->Identifier);
		OUT("CHOICE_constraint,\n");
		OUT("CHOICE_decode_ber,\n");
		OUT("CHOICE_encode_der,\n");
		OUT("CHOICE_print,\n");
		OUT("CHOICE_free,\n");
		OUT("CHOICE_outmost_tag,\n");
		if(tags_count) {
			OUT("asn1_DEF_%s_tags,\n", p);
			OUT("sizeof(asn1_DEF_%s_tags)\n", p);
			OUT("\t/sizeof(asn1_DEF_%s_tags[0]), /* %d */\n",
				p, tags_count);
		} else {
			OUT("0,\t/* No explicit tags (pointer) */\n");
			OUT("0,\t/* No explicit tags (count) */\n");
		}
		OUT("%d,\t/* Tags to skip */\n", tags_impl_skip);
		OUT("%d,\t/* Whether CONSTRUCTED */\n", 1);
		OUT("&asn1_DEF_%s_specs\t/* Additional specs */\n", p);
	);
	OUT("};\n");
	OUT("\n");

	REDIR(OT_TYPE_DECLS);

	return 0;
}

int
asn1c_lang_C_type_REFERENCE(arg_t *arg) {
	asn1p_ref_t *ref;

	ref = arg->expr->reference;
	if(ref->components[ref->comp_count-1].name[0] == '&') {
		asn1p_module_t *mod;
		asn1p_expr_t *extract;
		arg_t tmp;
		int ret;

		extract = asn1f_class_access_ex(arg->asn, arg->mod, arg->expr,
			ref, &mod);
		if(extract == NULL)
			return -1;

		extract = asn1p_expr_clone(extract);
		if(extract) {
			if(extract->Identifier)
				free(extract->Identifier);
			extract->Identifier = strdup(arg->expr->Identifier);
			if(extract->Identifier == NULL) {
				asn1p_expr_free(extract);
				return -1;
			}
		} else {
			return -1;
		}

		tmp = *arg;
		tmp.asn = arg->asn;
		tmp.mod = mod;
		tmp.expr = extract;

		ret = arg->default_cb(&tmp);

		asn1p_expr_free(extract);

		return ret;
	}


	return asn1c_lang_C_type_SIMPLE_TYPE(arg);
}

int
asn1c_lang_C_type_SIMPLE_TYPE(arg_t *arg) {
	asn1p_expr_t *expr = arg->expr;
	int tags_impl_skip = 0;
	int tags_count;
	char *p;

	if(arg->embed) {
		REDIR(OT_TYPE_DECLS);

		OUT("%s\t", asn1c_type_name(arg, arg->expr,
			expr->marker?TNF_RSAFE:TNF_CTYPE));
		OUT("%s", expr->marker?"*":" ");
		OUT("%s;", MKID(expr->Identifier));
		if(expr->marker) OUT("\t/* %s */",
			(expr->marker==EM_OPTIONAL)?"OPTIONAL":"DEFAULT");
		OUT("\n");
		return 0;
	}


	GEN_INCLUDE(asn1c_type_name(arg, expr, TNF_INCLUDE));

	REDIR(OT_TYPE_DECLS);

	OUT("typedef %s\t", asn1c_type_name(arg, arg->expr, TNF_CTYPE));
	OUT("%s", expr->marker?"*":" ");
	OUT("%s_t;\n", MKID(expr->Identifier));
	OUT("\n");

	REDIR(OT_STAT_DEFS);

	/*
	 * Print out asn1_DEF_<type>_tags[] vector.
	 */
	tags_count = emit_tags_vector(arg, expr, &tags_impl_skip, 0);

	p = MKID(expr->Identifier);
	OUT("asn1_TYPE_descriptor_t asn1_DEF_%s = {\n", p);
	INDENTED(
		OUT("\"%s\",\n", expr->Identifier);
		OUT("%s_constraint,\n", p);
		OUT("%s_decode_ber,\n", p);
		OUT("%s_encode_der,\n", p);
		OUT("%s_print,\n", p);
		OUT("%s_free,\n", p);
		OUT("0,\t/* Use generic outmost tag fetcher */\n");
		if(tags_count) {
			OUT("asn1_DEF_%s_tags,\n", p);
			OUT("sizeof(asn1_DEF_%s_tags)\n", p);
			OUT("\t/sizeof(asn1_DEF_%s_tags[0]), /* %d */\n",
				p, tags_count);
		} else {
			OUT("0,\t/* No explicit tags (pointer) */\n");
			OUT("0,\t/* No explicit tags (count) */\n");
		}
		OUT("%d,\t/* Tags to skip */\n", tags_impl_skip);
		OUT("-0,\t/* Unknown yet */\n");
		OUT("0\t/* No specifics */\n");
	);
	OUT("};\n");
	OUT("\n");

	/*
	 * Constraint checking.
	 */
	/* Emit FROM() tables and others */
	asn1c_emit_constraint_tables(arg, 0);

	p = MKID(expr->Identifier);
	OUT("int\n");
	OUT("%s_constraint(asn1_TYPE_descriptor_t *td, const void *sptr,\n", p);
	INDENTED(
	OUT("\t\tasn_app_consume_bytes_f *app_errlog, void *app_key) {\n");
	OUT("\n");
	if(asn1c_emit_constraint_checking_code(arg) == 1) {
		if(0) {
		OUT("/* Check the constraints of the underlying type */\n");
		OUT("return asn1_DEF_%s.check_constraints\n",
			asn1c_type_name(arg, expr, TNF_SAFE));
		OUT("\t(td, sptr, app_errlog, app_key);\n");
		} else {
		OUT("/* Make the underlying type checker permanent */\n");
		OUT("td->check_constraints = asn1_DEF_%s.check_constraints;\n",
			asn1c_type_name(arg, expr, TNF_SAFE));
		OUT("return td->check_constraints\n");
		OUT("\t(td, sptr, app_errlog, app_key);\n");
		}
	}
	);
	OUT("}\n");
	OUT("\n");

	/*
	 * Emit suicidal functions.
	 */

	{
	/*
	 * This function replaces certain fields from the definition
	 * of a type with the corresponding fields from the basic type
	 * (from which the current type is inherited).
	 */
	char *type_name = asn1c_type_name(arg, expr, TNF_SAFE);
	OUT("/*\n");
	OUT(" * This type is implemented using %s,\n", type_name);
	OUT(" * so adjust the DEF appropriately.\n");
	OUT(" */\n");
	OUT("static void\n");
	OUT("inherit_TYPE_descriptor(asn1_TYPE_descriptor_t *td) {\n");
	INDENT(+1);
	OUT("td->ber_decoder = asn1_DEF_%s.ber_decoder;\n", type_name);
	OUT("td->der_encoder = asn1_DEF_%s.der_encoder;\n", type_name);
	OUT("td->free_struct = asn1_DEF_%s.free_struct;\n", type_name);
	OUT("td->print_struct = asn1_DEF_%s.print_struct;\n", type_name);
	OUT("td->last_tag_form = asn1_DEF_%s.last_tag_form;\n", type_name);
	OUT("td->specifics = asn1_DEF_%s.specifics;\n", type_name);
	INDENT(-1);
	OUT("}\n");
	OUT("\n");
	}

	p = MKID(expr->Identifier);
	OUT("ber_dec_rval_t\n");
	OUT("%s_decode_ber(asn1_TYPE_descriptor_t *td,\n", p);
	INDENTED(
	OUT("\tvoid **structure, void *bufptr, size_t size, int tag_mode) {\n");
	OUT("inherit_TYPE_descriptor(td);\n");
	OUT("return td->ber_decoder(td, structure,\n");
	OUT("\tbufptr, size, tag_mode);\n");
	);
	OUT("}\n");
	OUT("\n");

	p = MKID(expr->Identifier);
	OUT("der_enc_rval_t\n");
	OUT("%s_encode_der(asn1_TYPE_descriptor_t *td,\n", p);
	INDENTED(
	OUT("\tvoid *structure, int tag_mode, ber_tlv_tag_t tag,\n");
	OUT("\tasn_app_consume_bytes_f *cb, void *app_key) {\n");
	OUT("inherit_TYPE_descriptor(td);\n");
	OUT("return td->der_encoder(td, structure, tag_mode, tag, cb, app_key);\n");
	);
	OUT("}\n");
	OUT("\n");

	p = MKID(expr->Identifier);
	OUT("int\n");
	OUT("%s_print(asn1_TYPE_descriptor_t *td, const void *struct_ptr,\n", p);
	INDENTED(
	OUT("\tint ilevel, asn_app_consume_bytes_f *cb, void *app_key) {\n");
	OUT("inherit_TYPE_descriptor(td);\n");
	OUT("return td->print_struct(td, struct_ptr, ilevel, cb, app_key);\n");
	);
	OUT("}\n");
	OUT("\n");

	p = MKID(expr->Identifier);
	OUT("void\n");
	OUT("%s_free(asn1_TYPE_descriptor_t *td,\n", p);
	INDENTED(
	OUT("\tvoid *struct_ptr, int contents_only) {\n");
	OUT("inherit_TYPE_descriptor(td);\n");
	OUT("td->free_struct(td, struct_ptr, contents_only);\n");
	);
	OUT("}\n");
	OUT("\n");

	REDIR(OT_FUNC_DECLS);

	p = MKID(expr->Identifier);
	OUT("extern asn1_TYPE_descriptor_t asn1_DEF_%s;\n", p);
	OUT("asn_constr_check_f %s_constraint;\n", p);
	OUT("ber_type_decoder_f %s_decode_ber;\n", p);
	OUT("der_type_encoder_f %s_encode_der;\n", p);
	OUT("asn_struct_print_f %s_print;\n", p);
	OUT("asn_struct_free_f %s_free;\n", p);

	REDIR(OT_TYPE_DECLS);

	return 0;
}

int
asn1c_lang_C_type_EXTENSIBLE(arg_t *arg) {

	OUT("/*\n");
	OUT(" * This type is extensible,\n");
	OUT(" * possible extensions are below.\n");
	OUT(" */\n");

	return 0;
}

static int check_if_extensible(asn1p_expr_t *expr) {
	asn1p_expr_t *v;
	TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE) return 1;
	}
	return 0;
}

static int
_print_tag(arg_t *arg, asn1p_expr_t *expr, struct asn1p_type_tag_s *tag_p) {
	struct asn1p_type_tag_s tag;

	if(tag_p) {
		tag = *tag_p;
	} else {
		if(asn1f_fetch_tag(arg->asn, arg->mod, expr, &tag)) {
			OUT("-1 /* Ambiguous tag (CHOICE?) */");
			return 0;
		}
	}

	OUT("(");
	switch(tag.tag_class) {
	case TC_UNIVERSAL:		OUT("ASN_TAG_CLASS_UNIVERSAL"); break;
	case TC_APPLICATION:		OUT("ASN_TAG_CLASS_APPLICATION"); break;
	case TC_CONTEXT_SPECIFIC:	OUT("ASN_TAG_CLASS_CONTEXT"); break;
	case TC_PRIVATE:		OUT("ASN_TAG_CLASS_PRIVATE"); break;
	case TC_NOCLASS:
		break;
	}
	OUT(" | (%lld << 2))", tag.tag_value);

	return 0;
}


static int
_tag2el_cmp(const void *ap, const void *bp) {
	const tag2el_t *a = ap;
	const tag2el_t *b = bp;
	const struct asn1p_type_tag_s *ta = &a->el_tag;
	const struct asn1p_type_tag_s *tb = &b->el_tag;

	if(ta->tag_class == tb->tag_class) {
		if(ta->tag_value == tb->tag_value) {
			/*
			 * Sort by their respective positions.
			 */
			if(a->el_no < b->el_no)
				return -1;
			else if(a->el_no > b->el_no)
				return 1;
			return 0;
		} else if(ta->tag_value < tb->tag_value)
			return -1;
		else
			return 1;
	} else if(ta->tag_class < tb->tag_class) {
		return -1;
	} else {
		return 1;
	}
}

/*
 * For constructed types, number of external tags may be greater than
 * number of elements in the type because of CHOICE type.
 * T ::= SET {		-- Three possible tags:
 *     a INTEGER,	-- One tag is here...
 *     b Choice1	-- ... and two more tags are there.
 * }
 * Choice1 ::= CHOICE {
 *     s1 IA5String,
 *     s2 ObjectDescriptor
 * }
 */
static int
_fill_tag2el_map(arg_t *arg, tag2el_t **tag2el, int *count, int el_no) {
	asn1p_expr_t *expr = arg->expr;
	arg_t tmparg = *arg;
	asn1p_expr_t *v;
	int element = 0;

	TQ_FOR(v, &(expr->members), next) {
		if(v->expr_type == A1TC_EXTENSIBLE)
			continue;

		tmparg.expr = v;

		if(_add_tag2el_member(&tmparg, tag2el, count,
				(el_no==-1)?element:el_no)) {
			return -1;
		}

		element++;
	}

	/*
	 * Sort the map according to canonical order of their tags
	 * and element numbers.
	 */
	qsort(*tag2el, *count, sizeof(**tag2el), _tag2el_cmp);

	/*
	 * Initialize .toff_{first|last} members.
	 */
	if(*count) {
		struct asn1p_type_tag_s *cur_tag = 0;
		tag2el_t *cur = *tag2el;
		tag2el_t *end = cur + *count;
		int occur, i;
		for(occur = 0; cur < end; cur++) {
			if(cur_tag == 0
			|| cur_tag->tag_value != cur->el_tag.tag_value
			|| cur_tag->tag_class != cur->el_tag.tag_class) {
				cur_tag = &cur->el_tag;
				occur = 0;
			} else {
				occur++;
			}
			cur->toff_first = -occur;
			for(i = 0; i >= -occur; i--)
				cur[i].toff_last = -i;
		}
	}

	return 0;
}

static int
_add_tag2el_member(arg_t *arg, tag2el_t **tag2el, int *count, int el_no) {
	struct asn1p_type_tag_s tag;
	int ret;

	assert(el_no >= 0);

	ret = asn1f_fetch_tag(arg->asn, arg->mod, arg->expr, &tag);
	if(ret == 0) {
		void *p;
		p = realloc(*tag2el, sizeof(tag2el_t) * ((*count) + 1));
		if(p)	*tag2el = p;
		else	return -1;

		DEBUG("Found tag for %s: %ld",
			arg->expr->Identifier,
			(long)tag.tag_value);

		(*tag2el)[*count].el_tag = tag;
		(*tag2el)[*count].el_no = el_no;
		(*tag2el)[*count].from_expr = arg->expr;
		(*count)++;
		return 0;
	}

	DEBUG("Searching tag in complex expression %s:%x at line %d",
		arg->expr->Identifier,
		arg->expr->expr_type,
		arg->expr->_lineno);

	/*
	 * Iterate over members of CHOICE type.
	 */
	if(arg->expr->expr_type == ASN_CONSTR_CHOICE) {
		return _fill_tag2el_map(arg, tag2el, count, el_no);
	}

	if(arg->expr->expr_type == A1TC_REFERENCE) {
		arg_t tmp = *arg;
		asn1p_expr_t *expr;
		expr = asn1f_lookup_symbol_ex(tmp.asn, &tmp.mod, tmp.expr,
			arg->expr->reference);
		if(expr) {
			tmp.expr = expr;
			return _add_tag2el_member(&tmp, tag2el, count, el_no);
		} else {
			FATAL("Cannot dereference %s at line %d",
				arg->expr->Identifier,
				arg->expr->_lineno);
			return -1;
		}
	}

	DEBUG("No tag for %s at line %d",
		arg->expr->Identifier,
		arg->expr->_lineno);

	return -1;
}

static int
emit_tag2member_map(arg_t *arg, tag2el_t *tag2el, int tag2el_count) {
	asn1p_expr_t *expr = arg->expr;

	OUT("static asn1_TYPE_tag2member_t asn1_DEF_%s_tag2el[] = {\n",
		MKID(expr->Identifier));
	if(tag2el_count) {
		int i;
		for(i = 0; i < tag2el_count; i++) {
			OUT("    { ");
			_print_tag(arg, expr, &tag2el[i].el_tag);
			OUT(", ");
			OUT("%d, ", tag2el[i].el_no);
			OUT("%d, ", tag2el[i].toff_first);
			OUT("%d ", tag2el[i].toff_last);
			OUT("}, /* %s at %d */\n",
				tag2el[i].from_expr->Identifier,
				tag2el[i].from_expr->_lineno
			);
		}
	}
	OUT("};\n");

	return 0;;
}

static int
emit_tags_vector(arg_t *arg, asn1p_expr_t *expr, int *tags_impl_skip, int choice_mode) {
	int tags_count = 0;
	int save_target = arg->target->target;
	char *p;

	if(save_target != OT_IGNORE) {
		int save_impl_skip = *tags_impl_skip;
		REDIR(OT_IGNORE);
		tags_count = emit_tags_vector(arg, expr,
			tags_impl_skip, choice_mode);
		REDIR(save_target);
		if(tags_count) {
			*tags_impl_skip = save_impl_skip;
			tags_count = 0;
		} else {
			return 0;
		}
	}
			

	p = MKID(expr->Identifier);
	OUT("static ber_tlv_tag_t asn1_DEF_%s_tags[] = {\n", p);
	INDENTED(
		if(expr->tag.tag_class) {
			tags_count++;
			_print_tag(arg, expr, &expr->tag);
			if(expr->tag.tag_mode != TM_EXPLICIT)
				(*tags_impl_skip)++;
		} else {
			if(!choice_mode)
				(*tags_impl_skip)++;
		}
		if(!choice_mode) {
			if(!expr->tag.tag_class
			|| (expr->meta_type == AMT_TYPE
				&& expr->tag.tag_mode == TM_EXPLICIT)) {
				struct asn1p_type_tag_s tag;
				if(expr->tag.tag_class)
					OUT(",\n");
				tag.tag_class = TC_UNIVERSAL;
				tag.tag_mode = TM_IMPLICIT;
				tag.tag_value = expr_type2uclass_value[expr->expr_type];
				_print_tag(arg, expr, &tag);
				tags_count++;
			}
		}
		OUT("\n");
	);
	OUT("};\n");

	return tags_count;
}
