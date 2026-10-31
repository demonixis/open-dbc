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

## Collisions

Each step adds a message for every pair of touching bodies that are awake, also when they only are about to touch.
`ODE COLLISION MESSAGE EXISTS` clears the current message and says whether more wait; `ODE COLLISION GET MESSAGE` takes
the newest. Up to 4096 wait unread, losing the oldest. Object A is a body that moves, the lower number of two that do,
with its contact flag at 1; a static object B has a flag of 0 and no velocity. Velocities are from just before the step
resolved the contact. The additions `ODE GET CONTACT POINT X/Y/Z`, `ODE GET CONTACT NORMAL X/Y/Z` and
`ODE GET CONTACT DEPTH` give the point on A, the direction from A to B and the overlap, which is 0 before they touch.
The plugin also reported pairs whose bounds overlapped without a contact, with both flags at 0; these do not exist here.

Two bodies bounce by the larger of their restitutions, which is on only while a body's surface mode says so:
`ODE SETSURFACE MODE CONTACT BOUNCE id, 1` applies `ODE SET CONTACT BOUNCE`, and below a body's
`ODE SET CONTACT BOUNCE VELOCITY` a contact does not bounce. Whatever is set, speeds under 1 unit per second do not
bounce, for stability. `ODE SET CONTACT FDIR1` sets the friction, as the plugin's export of that name does, and
`ODE SET CONTACT VELOCITY`, exported by the plugin without a definition, sets the bounce velocity. The general
`ODE SET CONTACT` takes modes 0, 2 and 3 and `ODE SETSURFACE MODE` mode 2; the other modes have no Jolt counterpart and
report an error.

## Rays

Each ray keeps its last hit for `ODE GET RAY COLLISION X/Y/Z`, `ODE GET RAY NORMAL X/Y/Z` and `ODE GET RAY OBJECT HIT`,
which gives the object number on every form where the plugin often gave 0. A miss sets the object to 0 and leaves the
point and normal. Rays skip disabled bodies and a convex shape they start inside, and meet triangles from both sides.

Static terrain meshes count as terrain for `ODE RAY`'s mask (1), capsules as capsules (4) and the rest as objects (2).
`ODE RAY CAST` tests one object's body, or all of them with object 0. `ODE RAY FORCE` adds a force along the ray at the
hit point of a moving body, for its next update.

## Solver

`ODE SET WORLD ERP` is Jolt's position correction per iteration, and `ODE SET WORLD QUICK STEP NUM ITERATIONS` its
velocity iterations, with 2 as the minimum because friction needs them. `ODE SET STEP MODE 1` stands for ODE's exact
solver, which has no Jolt counterpart, with at least 50 velocity and 10 position iterations. These settings last until
the next `ODE START`.

## Additions

These forms have no counterpart in the plugin: `ODE ADD TORQUE`, `ODE ADD IMPULSE`, `ODE SET BODY KINEMATIC`,
`ODE SET BODY DAMPING`, `ODE GET BODY ANGULAR VELOCITY X/Y/Z`, `ODE GET CONTACT POINT X/Y/Z`,
`ODE GET CONTACT NORMAL X/Y/Z` and `ODE GET CONTACT DEPTH`.
