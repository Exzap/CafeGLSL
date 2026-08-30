#define paste_field(n) tex_coord##n##.x
#define paste_index(n) cAmbColor[##n]
#define paste_ident(a,b) a##b
paste_field(0)
paste_index(2)
paste_ident(one, two)
