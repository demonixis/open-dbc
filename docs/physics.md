# Physics

The DBPro `ODE` forms run on Jolt Physics in the `DbPro` profile. `ODE START` opens an empty world and `ODE END`
closes it; a body belongs to the object of the same number. One world unit is one Jolt metre. Angles are degrees,
except in `ODE CREATE STATIC CYLINDER`, which takes radians as the plugin did.

[The reference](reference/dbpro_extensions_reference.md) lists every form and its status.

## Time

`ODE UPDATE` runs whole fixed steps of `ODE SET WORLD STEP` (1/60 s by default, 0.001 to 0.1), so the motion does not
depend on the frame rate; the time left over extrapolates what the objects show. Without an argument an update takes
the real time since the last one, up to 0.1 s, if a frame was presented since, and otherwise one regular step.

## Bodies

Shapes come from the object's vertices with its scale and pivot, or from `MAKE OBJECT COLLISION BOX` when the object
has one (not for spheres and meshes). Moving a body moves its object. Every body weighs 1 by default whatever its
size; `ODE SET BODY MASS` replaces that, and `ODE CREATE DYNAMIC BOX` takes a percentage of it.

- A **capsule** never turns, and its object keeps the rotation the program gives it. It stands on the object's lowest
  point, raised by the form's last argument. Its cylinder is 0.55 of the object's height times the scale modifier and
  its radius 0.967 of a quarter of width plus depth.
- A **static triangle mesh** is the object's real surface, and solid only on the side its faces show, because Jolt's
  simulation ignores the back of a triangle. A closed mesh is solid from outside; a floor made of a plain must face
  up, as with `XROTATE OBJECT 1, 270`.
- A **dynamic triangle mesh** is the convex hull of its object. A flat object has no volume to simulate and is refused:
  use `ODE CREATE DYNAMIC BOX`, which gives it a minimum thickness.
- `ODE CREATE DYNAMIC BOX` tip-over modes 1, 2 and 3 add a plate 1.5 times the box's size, thin along X, Y or Z. Mode
  11 removes the box from the collision response.

## Motion

`ODE ADD FORCE` acts for the whole of the next update, with its point relative to the body's centre and in world axes,
as in the plugin's last version; the older ODE plugin took a world position and multiplied the force by 10000.

`ODE SET RESPONSE` mode 2 and the addition `ODE SET BODY KINEMATIC` make a body kinematic: gravity and forces no longer
reach it, it follows its velocity and it pushes what it meets. Response mode 1 ignored `ODE CREATE STATIC UNIVERSE`'s
mesh, which does not exist here, so it behaves like mode 0.

`ODE SET RAY CAST` switches a body's continuous collision detection, which is on by default.

## Solver

`ODE SET WORLD ERP` is Jolt's position correction per iteration, and `ODE SET WORLD QUICK STEP NUM ITERATIONS` its
velocity iterations, with 2 as the minimum because friction needs them. `ODE SET STEP MODE 1` stands for ODE's exact
solver, which has no Jolt counterpart, with at least 50 velocity and 10 position iterations. These settings last until
the next `ODE START`.

## Additions

These forms have no counterpart in the plugin: `ODE ADD TORQUE`, `ODE ADD IMPULSE`, `ODE SET BODY KINEMATIC`,
`ODE SET BODY DAMPING` and `ODE GET BODY ANGULAR VELOCITY X/Y/Z`.
