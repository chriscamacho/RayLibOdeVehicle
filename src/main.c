/*
 * Copyright (c) 2021 Chris Camacho (codifies -  http://bedroomcoders.co.uk/)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "raylib.h"
#include "raymath.h"

#define RLIGHTS_IMPLEMENTATION
#include "rlights.h"

#include <ode/ode.h>
#include "raylibODE.h"

#include "assert.h"

/*
 * get ODE from https://bitbucket.org/odedevs/ode/downloads/
 *
 * extract ode 0.16.2 into the main directory of this project
 * 
 * cd into it
 * 
 * I'd suggest building it with this configuration
 * ./configure --enable-ou --enable-libccd --with-box-cylinder=libccd --with-drawstuff=none --disable-demos --with-libccd=internal
 *
 * and run make, you should then be set to compile this project
 */


// globals in use by near callback
dWorldID world;
dJointGroupID contactgroup;

Model box;
Model ball;
Model cylinder;

int numObj = 300; // number of bodies


inline float rndf(float min, float max);
// macro candidate ? marcro's? eek!

float rndf(float min, float max) 
{
    return ((float)rand() / (float)(RAND_MAX)) * (max - min) + min;
}



// when objects potentially collide this callback is called
// you can rule out certain collisions or use different surface parameters
// depending what object types collide.... lots of flexibility and power here!
#define MAX_CONTACTS 8

static void nearCallback(void *data, dGeomID o1, dGeomID o2)
{
    (void)data;
    int i;

    // exit without doing anything if the two bodies are connected by a joint
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);
    //if (b1==b2) return;
    if (b1 && b2 && dAreConnectedExcluding(b1, b2, dJointTypeContact))
        return;
        
    if (!checkColliding(o1)) return;
    if (!checkColliding(o2)) return;

    // getting these just so can sometimes be a little bit of a black art!
    dContact contact[MAX_CONTACTS]; // up to MAX_CONTACTS contacts per body-body
    for (i = 0; i < MAX_CONTACTS; i++) {
        contact[i].surface.mode = dContactSlip1 | dContactSlip2 |
                                    dContactSoftERP | dContactSoftCFM | dContactApprox1;
        contact[i].surface.mu = 1000;
        contact[i].surface.slip1 = 0.0001;
        contact[i].surface.slip2 = 0.001;
        contact[i].surface.soft_erp = 0.05;
        contact[i].surface.soft_cfm = 0.0003;
      
        contact[i].surface.bounce = 0.1;
        contact[i].surface.bounce_vel = 0.1;

    }
    int numc = dCollide(o1, o2, MAX_CONTACTS, &contact[0].geom,
                        sizeof(dContact));
    if (numc) {
        dMatrix3 RI;
        dRSetIdentity(RI);
        for (i = 0; i < numc; i++) {
            dJointID c =
                dJointCreateContact(world, contactgroup, contact + i);
            dJointAttach(c, b1, b2);
        }
    }

}


int main(void)
{
    assert(sizeof(dReal) == sizeof(float));
    srand ( time(NULL) );

    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 1920/2;
    const int screenHeight = 1080/2;

    // a space can have multiple "worlds" for example you might have different
    // sub levels that never interact, or the inside and outside of a building
    dSpaceID space;

    // create an array of bodies
    dBodyID obj[numObj];

    SetWindowState(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "raylib ODE and a car!");

    // Define the camera to look into our 3d world
    Camera camera = {(Vector3){ 25.0f, 15.0f, 25.0f }, (Vector3){ 0.0f, 0.5f, 0.0f },
                        (Vector3){ 0.0f, 1.0f, 0.0f }, 45.0f, CAMERA_PERSPECTIVE};

    box = LoadModelFromMesh(GenMeshCube(1,1,1));
    ball = LoadModelFromMesh(GenMeshSphere(.5,32,32));
    // alas gen cylinder is wrong orientation for ODE...
    // so rather than muck about at render time just make one the right orientation
    cylinder = LoadModel("data/cylinder.obj");
    
    Model ground = LoadModel("data/ground.obj");

    // texture the models
    Texture earthTx = LoadTexture("data/earth.png");
    Texture crateTx = LoadTexture("data/crate.png");
    Texture drumTx = LoadTexture("data/drum.png");
    Texture grassTx = LoadTexture("data/grass.png");

    box.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = crateTx;
    ball.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTx;
    cylinder.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = drumTx;
    ground.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = grassTx;

    Shader shader = LoadShader("data/simpleLight.vs", "data/simpleLight.fs");
    // load a shader and set up some uniforms
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

    
    // ambient light level
    int amb = GetShaderLocation(shader, "ambient");
    SetShaderValue(shader, amb, (float[4]){0.2,0.2,0.2,1.0}, SHADER_UNIFORM_VEC4);

    // models share the same shader
    box.materials[0].shader = shader;
    ball.materials[0].shader = shader;
    cylinder.materials[0].shader = shader;
    ground.materials[0].shader = shader;
    
    // using 4 point lights, white, red, green and blue
    Light lights[MAX_LIGHTS];

    lights[0] = CreateLight(LIGHT_POINT, (Vector3){ -25,25,25 }, Vector3Zero(),
                    (Color){128,128,128,255}, shader);
    lights[1] = CreateLight(LIGHT_POINT, (Vector3){ -25,25,-25 }, Vector3Zero(),
                    (Color){64,64,64,255}, shader);
/*                    
    lights[2] = CreateLight(LIGHT_POINT, (Vector3){ -25,25,-25 }, Vector3Zero(),
                    GREEN, shader);
    lights[3] = CreateLight(LIGHT_POINT, (Vector3){ -25,25,25 }, Vector3Zero(),
                    BLUE, shader);
*/

    dInitODE2(0);   // initialise and create the physics
    dAllocateODEDataForThread(dAllocateMaskAll);
    
    world = dWorldCreate();
    printf("phys iterations per step %i\n",dWorldGetQuickStepNumIterations(world));
    space = dHashSpaceCreate(NULL);
    contactgroup = dJointGroupCreate(0);
    dWorldSetGravity(world, 0, -9.8, 0);    // gravity
    
    dWorldSetAutoDisableFlag (world, 1);
    dWorldSetAutoDisableLinearThreshold (world, 0.05);
    dWorldSetAutoDisableAngularThreshold (world, 0.05);
    dWorldSetAutoDisableSteps (world, 4);


    vehicle* car = CreateVehicle(space, world);
    
    // create some decidedly sub optimal indices!
    // for the ground trimesh
    int nV = ground.meshes[0].vertexCount;
    int *groundInd = RL_MALLOC(nV*sizeof(int));
    for (int i = 0; i<nV; i++ ) {
        groundInd[i] = i;
    }
    
    // static tri mesh data to geom
    dTriMeshDataID triData = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(triData, ground.meshes[0].vertices,
                            3 * sizeof(float), nV,
                            groundInd, nV,
                            3 * sizeof(int));
    dCreateTriMesh(space, triData, NULL, NULL, NULL);
    

    // create the physics bodies
    for (int i = 0; i < numObj; i++) {
        obj[i] = dBodyCreate(world);
        dGeomID geom;
        dMatrix3 R;
        dMass m;
        float typ = rndf(0,1);
        if (typ < .25) {                //  box
            Vector3 s = (Vector3){rndf(0.25, .5), rndf(0.25, .5), rndf(0.25, .5)};
            geom = dCreateBox(space, s.x, s.y, s.z);
            dMassSetBox (&m, 10, s.x, s.y, s.z);
        } else if (typ < .5) {          //  sphere
            float r = rndf(0.125, .25);
            geom = dCreateSphere(space, r);
            dMassSetSphere(&m, 10, r);
        } else if (typ < .75) {         //  cylinder
            float l = rndf(0.125, .5);
            float r = rndf(0.125, .5);
            geom = dCreateCylinder(space, r, l);
            dMassSetCylinder(&m, 10, 3, r, l);
        } else {                        //  composite of cylinder with 2 spheres
            float l = rndf(.25,.5);
            
            geom = dCreateCylinder(space, 0.125, l);
            dGeomID geom2 = dCreateSphere(space, l/2);
            dGeomID geom3 = dCreateSphere(space, l/2);

            
            dMass m2,m3;
            dMassSetSphere(&m2, 5, l/2);
            dMassTranslate(&m2,0, 0, l - 0.125);
            dMassSetSphere(&m3, 5, l/2);
            dMassTranslate(&m3,0, 0, -l + 0.125);
            dMassSetCylinder(&m, 5, 3, .25, l);
            dMassAdd(&m2, &m3);
            dMassAdd(&m, &m2);
            
            dGeomSetBody(geom2, obj[i]);
            dGeomSetBody(geom3, obj[i]);
            dGeomSetOffsetPosition(geom2, 0, 0, l - 0.125);
            dGeomSetOffsetPosition(geom3, 0, 0, -l + 0.125);

        }

        // give the body a random position and rotation
        dBodySetPosition(obj[i],
                            dRandReal() * 40 - 5, 4+(i/10), dRandReal() * 40 - 5);
        dRFromAxisAndAngle(R, dRandReal() * 2.0 - 1.0,
                            dRandReal() * 2.0 - 1.0,
                            dRandReal() * 2.0 - 1.0,
                            dRandReal() * M_PI*2 - M_PI);
        dBodySetRotation(obj[i], R);
        // set the bodies mass and the newly created geometry
        dGeomSetBody(geom, obj[i]);
        dBodySetMass(obj[i], &m);


    }
    

    float accel=0,steer=0;
    Vector3 debug = {0};
    bool antiSway = true;
    
    // keep the physics fixed time in step with the render frame
    // rate which we don't know in advance
    float frameTime = 0; 
    float physTime = 0;
    const float physSlice = 1.0 / 240.0;
    const int maxPsteps = 6;
    int carFlipped = 0; // number of frames car roll is >90

    //--------------------------------------------------------------------------------------
    //
    // Main game loop
    //
    //--------------------------------------------------------------------------------------
    while (!WindowShouldClose())            // Detect window close button or ESC key
    {
        //--------------------------------------------------------------------------------------
        // Update
        //----------------------------------------------------------------------------------


        
        // extract just the roll of the car
        // count how many frames its >90 degrees either way
        const dReal* q = dBodyGetQuaternion(car->bodies[0]);
        float z0 = 2.0f*(q[0]*q[3] + q[1]*q[2]);
        float z1 = 1.0f - 2.0f*(q[1]*q[1] + q[3]*q[3]);
        float roll = atan2f(z0, z1);
        if ( fabs(roll) > (M_PI_2-0.001) ) {
            carFlipped++;
        } else {
            carFlipped=0;
        }
    
        // if the car roll >90 degrees for 100 frames then flip it
        if (carFlipped > 100) {
            unflipVehicle(car);
        }


        
        accel *= .99;
        if (IsKeyDown(KEY_UP)) accel +=2.5;
        if (IsKeyDown(KEY_DOWN)) accel -=2.5;
        if (accel > 75) accel = 75;
        if (accel < -25) accel = -25;
        
        
        if (IsKeyDown(KEY_RIGHT)) steer -=.1;
        if (IsKeyDown(KEY_LEFT)) steer +=.1;
        if (!IsKeyDown(KEY_RIGHT) && !IsKeyDown(KEY_LEFT)) steer *= .5;
        if (steer > .5) steer = .5;
        if (steer < -.5) steer = -.5;

              
        updateVehicle(car, accel, 800.0, steer, 10.0);


        const dReal* cp = dBodyGetPosition(car->bodies[0]);
        camera.target = (Vector3){cp[0],cp[1]+1,cp[2]};
        
        float lerp = 0.1f;

        dVector3 co;
        dBodyGetRelPointPos(car->bodies[0], -8, 3, 0, co);
        
        camera.position.x -= (camera.position.x - co[0]) * lerp  ;// * (1/ft);
        camera.position.y -= (camera.position.y - co[1])  * lerp ;// * (1/ft);
        camera.position.z -= (camera.position.z - co[2]) * lerp ;// * (1/ft);
        //UpdateCamera(&camera);
        
        bool spcdn = IsKeyDown(KEY_SPACE);
        
        for (int i = 0; i < numObj; i++) {
            const dReal* pos = dBodyGetPosition(obj[i]);
            if (spcdn) {
                // apply force if the space key is held
                const dReal* v = dBodyGetLinearVel(obj[0]);
                if (v[1] < 10 && pos[1]<10) { // cap upwards velocity and don't let it get too high
                    dBodyEnable (obj[i]); // case its gone to sleep
                    dMass mass;
                    dBodyGetMass (obj[i], &mass);
                    // give some object more force than others
                    float f = (6+(((float)i/numObj)*4)) * mass.mass;
                    dBodyAddForce(obj[i], rndf(-f,f), f*10, rndf(-f,f));
                }
            }

            
            if(pos[1]<-10) {
                // teleport back if fallen off the ground
                dBodySetPosition(obj[i], dRandReal() * 10 - 5,
                                        12 + rndf(1,2), dRandReal() * 10 - 5);
                dBodySetLinearVel(obj[i], 0, 0, 0);
                dBodySetAngularVel(obj[i], 0, 0, 0);
            }
        }
        
        //UpdateCamera(&camera);              // Update camera

        if (IsKeyPressed(KEY_L)) { lights[0].enabled = !lights[0].enabled; UpdateLightValues(shader, lights[0]);}
        
        // update the light shader with the camera view position
        SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], &camera.position.x, SHADER_UNIFORM_VEC3);

        frameTime += GetFrameTime();
        int pSteps = 0;
        physTime = GetTime(); 
        
        while (frameTime > physSlice) {
            // check for collisions
            // TODO use 2nd param data to pass custom structure with
            // world and space ID's to avoid use of globals...
            dSpaceCollide(space, 0, &nearCallback);
            
            // step the world
            dWorldQuickStep(world, physSlice);  // NB fixed time step is important
            dJointGroupEmpty(contactgroup);
            
            frameTime -= physSlice;
            pSteps++;
            if (pSteps > maxPsteps) {
                frameTime = 0;
                break;      
            }
        }
        
        physTime = GetTime() - physTime;    

        

        //----------------------------------------------------------------------------------
        // Draw
        //----------------------------------------------------------------------------------
     
        BeginDrawing();

        ClearBackground(BLACK);

        BeginMode3D(camera);
            DrawModel(ground,(Vector3){0,0,0},1,WHITE);
            
            // NB normally you wouldn't be drawing the collision meshes
            // instead you'd iterrate all the bodies get a user data pointer
            // from the body you'd previously set and use that to look up
            // what you are rendering oriented and positioned as per the
            // body
            drawAllSpaceGeoms(space);        
            DrawGrid(100, 1.0f);

        EndMode3D();

        //DrawFPS(10, 10); // can't see it in lime green half the time!!

        if (pSteps > maxPsteps) DrawText("WARNING CPU overloaded lagging real time", 10, 0, 20, RED);
        DrawText(TextFormat("%2i FPS", GetFPS()), 10, 20, 20, WHITE);
        DrawText(TextFormat("accel %4.4f",accel), 10, 40, 20, WHITE);
        DrawText(TextFormat("steer %4.4f",steer), 10, 60, 20, WHITE);
        if (!antiSway) DrawText("Anti sway bars OFF", 10, 80, 20, RED);
        DrawText(TextFormat("debug %4.4f %4.4f %4.4f",debug.x,debug.y,debug.z), 10, 100, 20, WHITE);
        DrawText(TextFormat("Phys steps per frame %i",pSteps), 10, 120, 20, WHITE);
        DrawText(TextFormat("Phys time per frame %f",physTime), 10, 140, 20, WHITE);
        DrawText(TextFormat("total time per frame %f",frameTime), 10, 160, 20, WHITE);
        DrawText(TextFormat("objects %i",numObj), 10, 180, 20, WHITE);

    
        DrawText(TextFormat("roll %.4f",fabs(roll)), 10, 200, 20, WHITE);
        
        const float* v = dBodyGetLinearVel(car->bodies[0]);
        float vel = Vector3Length((Vector3){v[0],v[1],v[2]}) * 2.23693629f;
        DrawText(TextFormat("mph %.4f",vel), 10, 220, 20, WHITE);
//printf("%i %i\n",pSteps, numObj);

        EndDrawing();

    }
        //----------------------------------------------------------------------------------


    //--------------------------------------------------------------------------------------
    // De-Initialization
    //--------------------------------------------------------------------------------------
    UnloadModel(box);
    UnloadModel(ball);
    UnloadModel(cylinder);
    UnloadModel(ground);
    UnloadTexture(drumTx);
    UnloadTexture(earthTx);
    UnloadTexture(crateTx);
    UnloadTexture(grassTx);
    UnloadShader(shader);
    
    RL_FREE(car);
    
    RL_FREE(groundInd);
    dGeomTriMeshDataDestroy(triData);

    dJointGroupEmpty(contactgroup);
    dJointGroupDestroy(contactgroup);
    dSpaceDestroy(space);
    dWorldDestroy(world);
    dCloseODE();

    CloseWindow();              // Close window and OpenGL context
    //--------------------------------------------------------------------------------------


    
    return 0;
}

