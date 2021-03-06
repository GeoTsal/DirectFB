/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Ported to GLES2.
 * Kristian Høgsberg <krh@bitplanet.net>
 * May 3, 2010
 *
 * Improve GLES2 port:
 *   * Refactor gear drawing.
 *   * Use correct normals for surfaces.
 *   * Improve shader.
 *   * Use perspective projection transformation.
 *   * Add FPS count.
 *   * Add comments.
 * Alexandros Frantzis <alexandros.frantzis@linaro.org>
 * Jul 13, 2010
 */


#include <directfb.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>


#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>


#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6


/**
 * Struct describing the vertices in triangle strip
 */
struct vertex_strip {
     /** The first vertex in the strip */
     GLint first;
     /** The number of consecutive vertices in the strip after the first */
     GLint count;
};

/* Each vertex consist of GEAR_VERTEX_STRIDE GLfloat attributes */
typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

/**
 * Struct representing a gear.
 */
struct gear {
     /** The array of vertices comprising the gear */
     GearVertex *vertices;
     /** The number of vertices comprising the gear */
     int nvertices;
     /** The array of triangle strips comprising the gear */
     struct vertex_strip *strips;
     /** The number of triangle strips comprising the gear */
     int nstrips;
     /** The Vertex Buffer Object holding the vertices in the graphics card */
     GLuint vbo;
};

/** The view rotation [x, y, z] */
static GLfloat view_rot[3] = { 20.0, 30.0, 0.0};
/** The gears */
static struct gear *gear1, *gear2, *gear3;
/** The current gear rotation angle */
static GLfloat angle = 0.0;
/** The location of the shader uniforms */
static GLuint ModelViewProjectionMatrix_location,
NormalMatrix_location,
LightSourcePosition_location,
MaterialColor_location;
/** The projection matrix */
static GLfloat ProjectionMatrix[16];
/** The direction of the directional light for the scene */
static const GLfloat LightSourcePosition[4] = { 5.0, 5.0, 10.0, 1.0};

/**
 * Fills a gear vertex.
 *
 * @param v the vertex to fill
 * @param x the x coordinate
 * @param y the y coordinate
 * @param z the z coortinate
 * @param n pointer to the normal table
 *
 * @return the operation error code
 */
static GearVertex *
vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
     v[0][0] = x;
     v[0][1] = y;
     v[0][2] = z;
     v[0][3] = n[0];
     v[0][4] = n[1];
     v[0][5] = n[2];

     return v + 1;
}

/**
 *  Create a gear wheel.
 *
 *  @param inner_radius radius of hole at center
 *  @param outer_radius radius at center of teeth
 *  @param width width of gear
 *  @param teeth number of teeth
 *  @param tooth_depth depth of tooth
 *
 *  @return pointer to the constructed struct gear
 */
static struct gear *
create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
            GLint teeth, GLfloat tooth_depth) {
     GLfloat r0, r1, r2;
     GLfloat da;
     GearVertex *v;
     struct gear *gear;
     double s[5], c[5];
     GLfloat normal[3];
     int cur_strip = 0;
     int i;

     /* Allocate memory for the gear */
     gear = malloc(sizeof *gear);
     if (gear == NULL)
          return NULL;

     /* Calculate the radii used in the gear */
     r0 = inner_radius;
     r1 = outer_radius - tooth_depth / 2.0;
     r2 = outer_radius + tooth_depth / 2.0;

     da = 2.0 * M_PI / teeth / 4.0;

     /* Allocate memory for the triangle strip information */
     gear->nstrips = STRIPS_PER_TOOTH * teeth;
     gear->strips = calloc(gear->nstrips, sizeof (*gear->strips));

     /* Allocate memory for the vertices */
     gear->vertices = calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
     v = gear->vertices;

     for (i = 0; i < teeth; i++) {
          /* Calculate needed sin/cos for varius angles */
          sincos(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
          sincos(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
          sincos(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
          sincos(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
          sincos(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

          /* A set of macros for making the creation of the gears easier */
#define  GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define  SET_NORMAL(x, y, z) do { \
   normal[0] = (x); normal[1] = (y); normal[2] = (z); \
} while(0)

#define  GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)

#define START_STRIP do { \
   gear->strips[cur_strip].first = v - gear->vertices; \
} while(0);

#define END_STRIP do { \
   int _tmp = (v - gear->vertices); \
   gear->strips[cur_strip].count = _tmp - gear->strips[cur_strip].first; \
   cur_strip++; \
} while (0)

#define QUAD_WITH_NORMAL(p1, p2) do { \
   SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
   v = GEAR_VERT(v, (p1), -1); \
   v = GEAR_VERT(v, (p1), 1); \
   v = GEAR_VERT(v, (p2), -1); \
   v = GEAR_VERT(v, (p2), 1); \
} while(0)

          struct point {
               GLfloat x;
               GLfloat y;
          };

          /* Create the 7 points (only x,y coords) used to draw a tooth */
          struct point p[7] = {
               GEAR_POINT(r2, 1), // 0
               GEAR_POINT(r2, 2), // 1
               GEAR_POINT(r1, 0), // 2
               GEAR_POINT(r1, 3), // 3
               GEAR_POINT(r0, 0), // 4
               GEAR_POINT(r1, 4), // 5
               GEAR_POINT(r0, 4), // 6
          };

          /* Front face */
          START_STRIP;
          SET_NORMAL(0, 0, 1.0);
          v = GEAR_VERT(v, 0, +1);
          v = GEAR_VERT(v, 1, +1);
          v = GEAR_VERT(v, 2, +1);
          v = GEAR_VERT(v, 3, +1);
          v = GEAR_VERT(v, 4, +1);
          v = GEAR_VERT(v, 5, +1);
          v = GEAR_VERT(v, 6, +1);
          END_STRIP;

          /* Inner face */
          START_STRIP;
          QUAD_WITH_NORMAL(4, 6);
          END_STRIP;

          /* Back face */
          START_STRIP;
          SET_NORMAL(0, 0, -1.0);
          v = GEAR_VERT(v, 6, -1);
          v = GEAR_VERT(v, 5, -1);
          v = GEAR_VERT(v, 4, -1);
          v = GEAR_VERT(v, 3, -1);
          v = GEAR_VERT(v, 2, -1);
          v = GEAR_VERT(v, 1, -1);
          v = GEAR_VERT(v, 0, -1);
          END_STRIP;

          /* Outer face */
          START_STRIP;
          QUAD_WITH_NORMAL(0, 2);
          END_STRIP;

          START_STRIP;
          QUAD_WITH_NORMAL(1, 0);
          END_STRIP;

          START_STRIP;
          QUAD_WITH_NORMAL(3, 1);
          END_STRIP;

          START_STRIP;
          QUAD_WITH_NORMAL(5, 3);
          END_STRIP;
     }

     gear->nvertices = (v - gear->vertices);

     /* Store the vertices in a vertex buffer object (VBO) */
     glGenBuffers(1, &gear->vbo);
     glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
     glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
                  gear->vertices, GL_STATIC_DRAW);

     return gear;
}

/**
 * Multiplies two 4x4 matrices.
 *
 * The result is stored in matrix m.
 *
 * @param m the first matrix to multiply
 * @param n the second matrix to multiply
 */
static void
multiply(GLfloat *m, const GLfloat *n)
{
     GLfloat tmp[16];
     const GLfloat *row, *column;
     div_t d;
     int i, j;

     for (i = 0; i < 16; i++) {
          tmp[i] = 0;
          d = div(i, 4);
          row = n + d.quot * 4;
          column = m + d.rem;
          for (j = 0; j < 4; j++)
               tmp[i] += row[j] * column[j * 4];
     }
     memcpy(m, &tmp, sizeof tmp);
}

/**
 * Rotates a 4x4 matrix.
 *
 * @param[in,out] m the matrix to rotate
 * @param angle the angle to rotate
 * @param x the x component of the direction to rotate to
 * @param y the y component of the direction to rotate to
 * @param z the z component of the direction to rotate to
 */
static void
rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
     double s, c;

     sincos(angle, &s, &c);
     GLfloat r[16] = {
          x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
          x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0,
          x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
          0, 0, 0, 1
     };

     multiply(m, r);
}


/**
 * Translates a 4x4 matrix.
 *
 * @param[in,out] m the matrix to translate
 * @param x the x component of the direction to translate to
 * @param y the y component of the direction to translate to
 * @param z the z component of the direction to translate to
 */
static void
translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
     GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1};

     multiply(m, t);
}

/**
 * Creates an identity 4x4 matrix.
 *
 * @param m the matrix make an identity matrix
 */
static void
identity(GLfloat *m)
{
     GLfloat t[16] = {
          1.0, 0.0, 0.0, 0.0,
          0.0, 1.0, 0.0, 0.0,
          0.0, 0.0, 1.0, 0.0,
          0.0, 0.0, 0.0, 1.0,
     };

     memcpy(m, t, sizeof(t));
}

/**
 * Transposes a 4x4 matrix.
 *
 * @param m the matrix to transpose
 */
static void
transpose(GLfloat *m)
{
     GLfloat t[16] = {
          m[0], m[4], m[8],  m[12],
          m[1], m[5], m[9],  m[13],
          m[2], m[6], m[10], m[14],
          m[3], m[7], m[11], m[15]};

     memcpy(m, t, sizeof(t));
}

/**
 * Inverts a 4x4 matrix.
 *
 * This function can currently handle only pure translation-rotation matrices.
 * Read http://www.gamedev.net/community/forums/topic.asp?topic_id=425118
 * for an explanation.
 */
static void
invert(GLfloat *m)
{
     GLfloat t[16];
     identity(t);

     // Extract and invert the translation part 't'. The inverse of a
     // translation matrix can be calculated by negating the translation
     // coordinates.
     t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];

     // Invert the rotation part 'r'. The inverse of a rotation matrix is
     // equal to its transpose.
     m[12] = m[13] = m[14] = 0;
     transpose(m);

     // inv(m) = inv(r) * inv(t)
     multiply(m, t);
}

/**
 * Calculate a perspective projection transformation.
 *
 * @param m the matrix to save the transformation in
 * @param fovy the field of view in the y direction
 * @param aspect the view aspect ratio
 * @param zNear the near clipping plane
 * @param zFar the far clipping plane
 */
static void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
     GLfloat tmp[16];
     identity(tmp);

     double sine, cosine, cotangent, deltaZ;
     GLfloat radians = fovy / 2 * M_PI / 180;

     deltaZ = zFar - zNear;
     sincos(radians, &sine, &cosine);

     if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
          return;

     cotangent = cosine / sine;

     tmp[0] = cotangent / aspect;
     tmp[5] = cotangent;
     tmp[10] = -(zFar + zNear) / deltaZ;
     tmp[11] = -1;
     tmp[14] = -2 * zNear * zFar / deltaZ;
     tmp[15] = 0;

     memcpy(m, tmp, sizeof(tmp));
}

/**
 * Draws a gear.
 *
 * @param gear the gear to draw
 * @param transform the current transformation matrix
 * @param x the x position to draw the gear at
 * @param y the y position to draw the gear at
 * @param angle the rotation angle of the gear
 * @param color the color of the gear
 */
static void
draw_gear(struct gear *gear, GLfloat *transform,
          GLfloat x, GLfloat y, GLfloat angle, const GLfloat color[4])
{
     GLfloat model_view[16];
     GLfloat normal_matrix[16];
     GLfloat model_view_projection[16];

     /* Translate and rotate the gear */
     memcpy(model_view, transform, sizeof (model_view));
     translate(model_view, x, y, 0);
     rotate(model_view, 2 * M_PI * angle / 360.0, 0, 0, 1);

     /* Create and set the ModelViewProjectionMatrix */
     memcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
     multiply(model_view_projection, model_view);

     glUniformMatrix4fv(ModelViewProjectionMatrix_location, 1, GL_FALSE,
                        model_view_projection);

     /*
      * Create and set the NormalMatrix. It's the inverse transpose of the
      * ModelView matrix.
      */
     memcpy(normal_matrix, model_view, sizeof (normal_matrix));
     invert(normal_matrix);
     transpose(normal_matrix);
     glUniformMatrix4fv(NormalMatrix_location, 1, GL_FALSE, normal_matrix);

     /* Set the gear color */
     glUniform4fv(MaterialColor_location, 1, color);

     /* Set the vertex buffer object to use */
     glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);

     /* Set up the position of the attributes in the vertex buffer object */
     glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                           6 * sizeof(GLfloat), NULL);
     glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                           6 * sizeof(GLfloat), (GLfloat *) 0 + 3);

     /* Enable the attributes */
     glEnableVertexAttribArray(0);
     glEnableVertexAttribArray(1);

     /* Draw the triangle strips that comprise the gear */
     int n;
     for (n = 0; n < gear->nstrips; n++)
          glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);

     /* Disable the attributes */
     glDisableVertexAttribArray(1);
     glDisableVertexAttribArray(0);
}

/**
 * Draws the gears.
 */
static void
gears_draw(void)
{
     const static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0};
     const static GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0};
     const static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0};
     GLfloat transform[16];
     identity(transform);

     glClearColor(0.0, 0.7, 1.0, 1.0);
     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

     /* Translate and rotate the view */
     translate(transform, 0, 0, -20);
     rotate(transform, 2 * M_PI * view_rot[0] / 360.0, 1, 0, 0);
     rotate(transform, 2 * M_PI * view_rot[1] / 360.0, 0, 1, 0);
     rotate(transform, 2 * M_PI * view_rot[2] / 360.0, 0, 0, 1);

     /* Draw the gears */
     draw_gear(gear1, transform, -3.0, -2.0, angle, red);
     draw_gear(gear2, transform, 3.1, -2.0, -2 * angle - 9.0, green);
     draw_gear(gear3, transform, -3.1, 4.2, -2 * angle - 25.0, blue);
}

/**
 * Handles a new window size or exposure.
 *
 * @param width the window width
 * @param height the window height
 */
static void
gears_reshape(int width, int height)
{
     /* Update the projection matrix */
     perspective(ProjectionMatrix, 60.0, width / (float)height, 1.0, 1024.0);

     /* Set the viewport */
     glViewport(0, 0, (GLint) width, (GLint) height);
}

static void
gears_tick(void)
{
     static int frames = 0;
     static double tRot0 = -1.0, tRate0 = -1.0;
     double dt, t = direct_clock_get_time( DIRECT_CLOCK_MONOTONIC ) / 1000000.0;

     if (tRot0 < 0.0)
          tRot0 = t;
     dt = t - tRot0;
     tRot0 = t;

     /* advance rotation for next frame */
     angle += 70.0 * dt;  /* 70 degrees per second */
     if (angle > 3600.0)
          angle -= 3600.0;

     frames++;

     if (tRate0 < 0.0)
          tRate0 = t;
     if (t - tRate0 >= 5.0) {
          GLfloat seconds = t - tRate0;
          GLfloat fps = frames / seconds;
          printf("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds,
                 fps);
          tRate0 = t;
          frames = 0;
     }
}

static const char vertex_shader[] =
"attribute vec3 position;\n"
"attribute vec3 normal;\n"
"\n"
"uniform mat4 ModelViewProjectionMatrix;\n"
"uniform mat4 NormalMatrix;\n"
"uniform vec4 LightSourcePosition;\n"
"uniform vec4 MaterialColor;\n"
"\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    // Transform the normal to eye coordinates\n"
"    vec3 N = normalize(vec3(NormalMatrix * vec4(normal, 1.0)));\n"
"\n"
"    // The LightSourcePosition is actually its direction for directional light\n"
"    vec3 L = normalize(LightSourcePosition.xyz);\n"
"\n"
"    // Multiply the diffuse value by the vertex color (which is fixed in this case)\n"
"    // to get the actual color that we will use to draw this vertex with\n"
"    float diffuse = max(dot(N, L), 0.0);\n"
"    Color = diffuse * MaterialColor;\n"
"\n"
"    // Transform the position to clip coordinates\n"
"    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);\n"
"}";

static const char fragment_shader[] =
"precision mediump float;\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    gl_FragColor = Color;\n"
"}";

static void
gears_init(void)
{
     GLuint v, f, program;
     const char *p;

     glEnable(GL_CULL_FACE);
     glEnable(GL_DEPTH_TEST);

     program = glCreateProgram();

     /* Compile the vertex shader */
     p = vertex_shader;
     v = glCreateShader(GL_VERTEX_SHADER);
     glShaderSource(v, 1, &p, NULL);
     glCompileShader(v);


     GLint  log_length, char_count;
     char *log;


     GLint status;

     glGetShaderiv(v, GL_COMPILE_STATUS, &status);
     if (status) {
          glAttachShader(program, v);
          glDeleteShader(v); // mark for deletion on detach
     }
     else {
          glGetShaderiv(v, GL_INFO_LOG_LENGTH, &log_length);

          log = malloc(log_length);

          glGetShaderInfoLog(v, log_length, &char_count, log);

          fprintf(stderr,"%s: vertex shader compilation failure:\n%s\n", __FUNCTION__, log);
          free(log);
     }


     /* Compile the fragment shader */
     p = fragment_shader;
     f = glCreateShader(GL_FRAGMENT_SHADER);
     glShaderSource(f, 1, &p, NULL);
     glCompileShader(f);


     glGetShaderiv(f, GL_COMPILE_STATUS, &status);
     if (status) {
          glAttachShader(program, f);
          glDeleteShader(f); // mark for deletion on detach
     }
     else {
          glGetShaderiv(f, GL_INFO_LOG_LENGTH, &log_length);

          log = malloc(log_length);

          glGetShaderInfoLog(f, log_length, &char_count, log);

          fprintf(stderr,"%s: fragment shader compilation failure:\n%s\n", __FUNCTION__, log);
          free(log);
     }



     /* Create and link the shader program */
//     glAttachShader(program, v);
//     glAttachShader(program, f);
     glBindAttribLocation(program, 0, "position");
     glBindAttribLocation(program, 1, "normal");

     glLinkProgram(program);
     glValidateProgram(program);


     glGetProgramiv(program, GL_LINK_STATUS, &status);


     // Report errors.  Shader objects detached when program is deleted.
     glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);

     log = malloc(log_length);

     glGetProgramInfoLog(program, log_length, &char_count, log);
     if (log[0])
          fprintf(stderr,"%s: shader program link log:\n%s\n", __FUNCTION__, log);
     free(log);


     /* Enable the shaders */
     glUseProgram(program);

     /* Get the locations of the uniforms so we can access them */
     ModelViewProjectionMatrix_location = glGetUniformLocation(program, "ModelViewProjectionMatrix");
     NormalMatrix_location = glGetUniformLocation(program, "NormalMatrix");
     LightSourcePosition_location = glGetUniformLocation(program, "LightSourcePosition");
     MaterialColor_location = glGetUniformLocation(program, "MaterialColor");

     /* Set the LightSourcePosition uniform which is constant throught the program */
     glUniform4fv(LightSourcePosition_location, 1, LightSourcePosition);

     /* make the gears */
     gear1 = create_gear(1.0, 4.0, 1.0, 20, 0.7);
     gear2 = create_gear(0.5, 2.0, 2.0, 10, 0.7);
     gear3 = create_gear(1.3, 2.0, 0.5, 10, 0.7);
}









typedef struct {
     IDirectFB             *dfb;
     IDirectFBSurface      *primary;
     IDirectFBEventBuffer  *events;
     DFBDimension           size;
} Test;


static DFBResult
Initialize( Test   *test,
            int    *argc,
            char ***argv )
{
     DFBResult             ret;
     DFBSurfaceDescription dsc;

     /*
      * Initialize DirectFB options
      */
     ret = DirectFBInit( argc, argv );
     if (ret) {
          D_DERROR( ret, "DirectFBInit() failed!\n" );
          return ret;
     }

     /*
      * Create the super interface
      */
     ret = DirectFBCreate( &test->dfb );
     if (ret) {
          D_DERROR( ret, "DirectFBCreate() failed!\n" );
          return ret;
     }

     /*
      * Create an event buffer for all devices with these caps
      */
     ret = test->dfb->CreateInputEventBuffer( test->dfb, DICAPS_KEYS | DICAPS_AXES, DFB_FALSE, &test->events );
     if (ret) {
          D_DERROR( ret, "IDirectFB::CreateInputEventBuffer( DICAPS_KEYS | DICAPS_AXES ) failed!\n" );
          return ret;
     }

     /*
      * Try to set our cooperative level to DFSCL_FULLSCREEN for exclusive access to the primary layer
      */
     test->dfb->SetCooperativeLevel( test->dfb, DFSCL_FULLSCREEN );

     /*
      * Create the primary surface
      */
     dsc.flags = DSDESC_CAPS;
     dsc.caps  = DSCAPS_PRIMARY | DSCAPS_FLIPPING | DSCAPS_GL;

     ret = test->dfb->CreateSurface( test->dfb, &dsc, &test->primary );
     if (ret) {
          D_DERROR( ret, "IDirectFB::CreateSurface( DSCAPS_PRIMARY | DSCAPS_FLIPPING ) failed!\n" );
          return ret;
     }

     /*
      * Get the size of the surface, clear and show it
      */
     test->primary->GetSize( test->primary, &test->size.w, &test->size.h );

//     test->primary->Clear( test->primary, 0, 0, 0, 0 );
//     test->primary->Flip( test->primary, NULL, 0 );


     return DFB_OK;
}

static void
Shutdown( Test *test )
{
     if (test->primary)
          test->primary->Release( test->primary );

     if (test->events)
          test->events->Release( test->events );

     if (test->dfb)
          test->dfb->Release( test->dfb );
}







static EGLDisplay display;
static EGLConfig configs[2];
static EGLContext context;
static EGLSurface surface;


static DFBResult
InitGL( Test *test )
{
     EGLint major, minor, nconfigs;
     EGLint attribs[] = {
          EGL_BUFFER_SIZE,     EGL_DONT_CARE,
          EGL_ALPHA_SIZE,      0,
          EGL_RED_SIZE,        0,
          EGL_GREEN_SIZE,      0,
          EGL_RED_SIZE,        0,
          EGL_SURFACE_TYPE,    0,
          EGL_DEPTH_SIZE,      16,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
          EGL_NONE
     };
     EGLint context_attrs[] = {
          EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE
     };
     EGLint surface_attrs[] = {
          EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE
     };
     EGLNativeDisplayType     disp = EGL_DEFAULT_DISPLAY;


#define EGL_CHECK(cmd)                                      \
     /*fprintf(stderr, "CALLING %s...\n", #cmd);*/              \
     if (cmd) {                                             \
          fprintf(stderr, "!!! %s failed\n", #cmd);         \
          goto quit;                                        \
     }

     // get display
     EGL_CHECK((display = eglGetDisplay(disp)) == EGL_NO_DISPLAY)

     // init
     EGL_CHECK(!eglInitialize(display, &major, &minor))


     eglGetConfigAttribsDIRECTFB( display, test->primary, attribs, 0 );


     // get configs
//     EGL_CHECK(!eglGetConfigs(display, configs, 2, &nconfigs))

     // choose config
     EGL_CHECK(!eglChooseConfig(display, attribs, configs, 2, &nconfigs))


     // create a surface
     EGL_CHECK((surface = eglCreateWindowSurface(display, configs[0], test->primary, surface_attrs)) == EGL_NO_SURFACE)

     EGL_CHECK(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)

     // create context
     EGL_CHECK((context = eglCreateContext(display, configs[0], EGL_NO_CONTEXT, context_attrs)) == EGL_NO_CONTEXT)

     EGL_CHECK(eglMakeCurrent(display, surface, surface, context) != EGL_TRUE)

     //eglQuerySurface(display, surface, EGL_WIDTH, &width);
     //eglQuerySurface(display, surface, EGL_HEIGHT, &height);

//     EGLint render_buffer = 0;
//     EGL_CHECK(!eglQuerySurface(display, surface, EGL_RENDER_BUFFER, &render_buffer));
//     fprintf(stderr,"RENDER_BUFFER = 0x%04x\n", render_buffer );


     const GLubyte *renderer = glGetString( GL_RENDERER );

     printf( "GL Renderer: '%s'\n", renderer );


     eglSwapInterval( display, 1 );


     /* Setup the viewport */
     glViewport( 0, 0, (GLint) test->size.w, (GLint) test->size.h );

     return DFB_OK;

quit:
     return DFB_FAILURE;
}

/**********************************************************************************************************************/
/**********************************************************************************************************************/

int
main( int argc, char *argv[] )
{
     DFBResult ret;
     bool      quit = false;
     Test      test;

     memset( &test, 0, sizeof(test) );


     ret = Initialize( &test, &argc, &argv );
     if (ret)
          goto error;

     ret = InitGL( &test );
     if (ret)
          goto error;

     gears_init();

     gears_reshape( test.size.w, test.size.h );

     /*
      * Main Loop
      */
     while (!quit) {
          DFBInputEvent evt;

          gears_draw();

          eglSwapBuffers( display, surface );

          gears_tick();

          /*
           * Process events
           */
          while (test.events->GetEvent( test.events, DFB_EVENT(&evt) ) == DFB_OK) {
               switch (evt.type) {
                    case DIET_KEYPRESS:
                         switch (evt.key_symbol) {
                              case DIKS_CURSOR_LEFT:
                                   view_rot[1] += 5.0;
                                   break;
                              case DIKS_CURSOR_RIGHT:
                                   view_rot[1] -= 5.0;
                                   break;
                              case DIKS_CURSOR_UP:
                                   view_rot[0] += 5.0;
                                   break;
                              case DIKS_CURSOR_DOWN:
                                   view_rot[0] -= 5.0;
                                   break;
                              case DIKS_ESCAPE:
                                   quit = true;
                                   break;
                              default:
                                   ;
                         }
                         break;
                    case DIET_KEYRELEASE:
                         switch (evt.key_symbol) {
                              default:
                                   ;
                         }
                         break;
                    case DIET_AXISMOTION:
                         if (evt.flags & DIEF_AXISREL) {
                              switch (evt.axis) {
                                   default:
                                        ;
                              }
                         }
                         break;
                    default:
                         ;
               }
          }
     }


error:
     Shutdown( &test );

     return ret;
}

