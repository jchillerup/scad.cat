// parametrix example model
// A cube base with a cylinder on top
$fn = 48;

cube_w = 20;
cube_h = 10;
cyl_r  = 6;
cyl_h  = 15;

union() {
    translate([0, 0, cube_h / 2])
        cube([cube_w, cube_w, cube_h], center = true);
    translate([0, 0, cube_h])
        cylinder(h = cyl_h, r = cyl_r, center = false);
}
