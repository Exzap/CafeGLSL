// NSMBU-shaped CLI regression shader: no #version, no binding qualifiers.

layout(std140) uniform MdlEnvView
{
    vec4 cView[4];
    vec4 cAmbColor[4];
};

layout(std140) uniform MdlMtx
{
    vec4 cWorld[3];
};

layout(std140) uniform Shp
{
    vec4 cShapeParam;
};

layout(std140) uniform Mat
{
    vec4 cTexMtx0;
    vec4 cTexMtx1;
};

in vec4 aPosition;
in vec4 tex_coord0;
in vec4 tex_coord1;

out vec4 vTexCoord;
out vec4 vColor;

#define expand_tex_coord( n )   tex_coord##n##.x
#define expand_amb( n )         cAmbColor[##n]
#define expand_mtx( n )         cTexMtx##n

#define calc_channel( n )                                          \
{                                                                  \
    vTexCoord[n] = dot( expand_mtx( n ), aPosition );              \
    vColor += expand_amb( n ) * expand_tex_coord( n );              \
}

void main()
{
    vColor = vec4( 0.0 );
    vTexCoord = vec4( 0.0 );

    calc_channel( 0 )
    calc_channel( 1 )

    vec4 world = vec4( dot( cWorld[0], aPosition ),
                       dot( cWorld[1], aPosition ),
                       dot( cWorld[2], aPosition ),
                       1.0 );

    gl_Position = vec4( dot( cView[0], world ), dot( cView[1], world ),
                        dot( cView[2], world ), dot( cView[3], world ) ) *
                  cShapeParam;
}
