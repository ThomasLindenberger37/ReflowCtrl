"""Create a configurable housing and ventilated snap-fit lid.

The PCB coordinate origin is its lower-left corner. All dimensions are in mm.
Configuration values are collected in the configuration section below.
"""

from dataclasses import dataclass
from math import sqrt
from pathlib import Path

from build123d import (
    Align,
    Box,
    BuildPart,
    BuildSketch,
    Cone,
    Cylinder,
    Locations,
    Plane,
    Polygon,
    Pos,
    RegularPolygon,
    Rot,
    SkipClean,
    SlotOverall,
    Text,
    export_stl,
    extrude,
)


# Data models


@dataclass(frozen=True)
class Point:
    """A two-dimensional point in PCB or housing coordinates."""

    x: float
    y: float


@dataclass(frozen=True)
class MountingHole:
    """A mounting position in PCB coordinates."""

    position: Point
    diameter: float


@dataclass(frozen=True)
class WallHoleCentre:
    """A hole centre on a wall parallel to the Y/Z plane."""

    y: float
    z: float


@dataclass(frozen=True)
class SnapPosition:
    """Position of a lid snap on the north or south housing wall."""

    x: float
    side: str


@dataclass(frozen=True)
class PcbConfig:
    width: float
    length: float
    mounting_holes: tuple[MountingHole, ...]


@dataclass(frozen=True)
class HousingConfig:
    base_thickness: float
    wall_thickness: float
    wall_height: float
    margin_north: float
    margin_east: float
    margin_south: float
    margin_west: float
    spacer_diameter: float
    spacer_height: float
    mounting_hole_chamfer: float
    auxiliary_mounts: tuple[MountingHole, ...]


@dataclass(frozen=True)
class VentConfig:
    hexagon_flat_to_flat: float
    web_width: float
    edge_clearance: float
    mount_clearance: float


@dataclass(frozen=True)
class CableSupportConfig:
    edge_inset: float
    pcb_clearance: float
    slot_count: int
    slot_length: float
    slot_width: float
    underside_recess_depth: float
    wall_hole_diameter: float
    wall_hole_clearance: float
    wall_hole_chamfer: float


@dataclass(frozen=True)
class LidConfig:
    thickness: float
    fit_clearance: float
    guide_depth: float
    guide_thickness: float
    guide_snap_clearance: float
    vent_edge_clearance: float
    closed_west_fraction: float
    snap_x_fractions: tuple[float, ...]
    snap_tab_width: float
    snap_tab_thickness: float
    snap_tab_length: float
    snap_hook_depth: float
    snap_hook_height: float
    snap_hook_bottom_offset: float
    receiver_clearance: float


@dataclass(frozen=True)
class ButtonActuatorConfig:
    pcb_position: Point
    height_above_spacers: float
    travel: float
    pin_tip_clearance: float
    pin_diameter: float
    flexure_width: float
    flexure_length: float
    flexure_thickness: float
    flexure_slot_width: float
    pin_offset_from_free_edge: float
    vent_keepout: float


@dataclass(frozen=True)
class LidLabelConfig:
    text: str
    font_path: str
    font_size: float


@dataclass(frozen=True)
class ExportConfig:
    directory_name: str
    housing_file_name: str
    lid_file_name: str


# Configuration


PCB_CONFIG = PcbConfig(
    width=80.0,
    length=45.0,
    mounting_holes=(
        MountingHole(Point(3.0, 3.0), diameter=2.8),
        MountingHole(Point(77.0, 3.0), diameter=2.8),
        MountingHole(Point(3.0, 42.0), diameter=2.8),
        MountingHole(Point(77.0, 42.0), diameter=2.8),
    ),
)

HOUSING_CONFIG = HousingConfig(
    base_thickness=3.0,
    wall_thickness=3.0,
    wall_height=28.0,
    margin_north=40.0,
    margin_east=5.0,
    margin_south=5.0,
    margin_west=50.0,
    spacer_diameter=8.0,
    spacer_height=8.0,
    mounting_hole_chamfer=0.8,
    auxiliary_mounts=(
        MountingHole(Point(18.0, 65.0), diameter=2.8),
    ),
)

VENT_CONFIG = VentConfig(
    hexagon_flat_to_flat=4.0,
    web_width=1.5,
    edge_clearance=5.0,
    mount_clearance=6.0,
)

LID_VENT_CONFIG = VentConfig(
    hexagon_flat_to_flat=5.0,
    web_width=1.5,
    edge_clearance=5.0,
    mount_clearance=6.0,
)

CABLE_SUPPORT_CONFIG = CableSupportConfig(
    edge_inset=3.0,
    pcb_clearance=10.0,
    slot_count=4,
    slot_length=16.0,
    slot_width=3.5,
    underside_recess_depth=5.0,
    wall_hole_diameter=7.5,
    wall_hole_clearance=0.0,
    wall_hole_chamfer=0.8,
)

LID_CONFIG = LidConfig(
    thickness=3.0,
    fit_clearance=0.3,
    guide_depth=5.0,
    guide_thickness=1.2,
    guide_snap_clearance=1.0,
    vent_edge_clearance=5.0,
    closed_west_fraction=1 / 3,
    snap_x_fractions=(1 / 4, 3 / 4),
    snap_tab_width=12.0,
    snap_tab_thickness=1.6,
    snap_tab_length=10.0,
    snap_hook_depth=1.2,
    snap_hook_height=1.5,
    snap_hook_bottom_offset=1.5,
    receiver_clearance=0.2,
)

BUTTON_ACTUATOR_CONFIG = ButtonActuatorConfig(
    pcb_position=Point(44.0, 4.5),
    height_above_spacers=6.6,
    travel=0.3,
    pin_tip_clearance=0.0,
    pin_diameter=4.0,
    flexure_width=14.0,
    flexure_length=18.0,
    flexure_thickness=1.2,
    flexure_slot_width=0.8,
    pin_offset_from_free_edge=4.0,
    vent_keepout=1.0,
)

LID_LABEL_CONFIG = LidLabelConfig(
    text="ReflowCtrl",
    font_path="assets/fonts/StardosStencil-Bold.ttf",
    font_size=15.0,
)

EXPORT_CONFIG = ExportConfig(
    directory_name="output_reflowHousing",
    housing_file_name="reflow_oven_controller_housing",
    lid_file_name="reflow_oven_controller_snap_fit_lid",
)

BOOLEAN_OVERLAP = 0.1
HEXAGON_SIDE_COUNT = 6
HEXAGON_ROTATION_DEGREES = 30.0


# Geometry helpers


def pcb_origin(housing: HousingConfig) -> Point:
    """Return the PCB origin in housing coordinates."""
    return Point(housing.margin_west, housing.margin_south)


def to_housing_coordinates(point: Point, housing: HousingConfig) -> Point:
    """Convert a point from PCB coordinates to housing coordinates."""
    origin = pcb_origin(housing)
    return Point(point.x + origin.x, point.y + origin.y)


def housing_footprint(pcb: PcbConfig, housing: HousingConfig) -> tuple[float, float]:
    """Calculate the outer width and length of the housing base."""
    width = pcb.width + housing.margin_west + housing.margin_east
    length = pcb.length + housing.margin_south + housing.margin_north
    return width + housing.base_thickness, length + housing.base_thickness


def all_mounting_holes(
    pcb: PcbConfig,
    housing: HousingConfig,
) -> tuple[MountingHole, ...]:
    """Return PCB and auxiliary mounts in one collection."""
    return pcb.mounting_holes + housing.auxiliary_mounts


def squared_distance(first: Point, second: Point) -> float:
    """Calculate squared distance without an unnecessary square root."""
    return (first.x - second.x) ** 2 + (first.y - second.y) ** 2


def make_cylinder(
    centre: Point,
    diameter: float,
    height: float,
    bottom_z: float,
):
    """Create a vertical cylinder at a housing-coordinate position."""
    return Pos(centre.x, centre.y, bottom_z) * Cylinder(
        diameter / 2,
        height,
        align=(Align.CENTER, Align.CENTER, Align.MIN),
    )


def make_spacer(mount: MountingHole, housing: HousingConfig):
    """Create one PCB spacer from a PCB-coordinate mount."""
    centre = to_housing_coordinates(mount.position, housing)
    return make_cylinder(
        centre=centre,
        diameter=housing.spacer_diameter,
        height=housing.spacer_height,
        bottom_z=housing.base_thickness,
    )


def make_mounting_hole_cut(mount: MountingHole, housing: HousingConfig):
    """Create a chamfered blind-hole cutter for one mounting spacer."""
    centre = to_housing_coordinates(mount.position, housing)
    hole_radius = mount.diameter / 2
    chamfer = housing.mounting_hole_chamfer
    chamfer_radius = hole_radius + chamfer
    hole_bottom_z = housing.wall_thickness
    hole_top_z = housing.base_thickness + housing.spacer_height
    hole_depth = hole_top_z - hole_bottom_z

    if hole_depth <= 0:
        raise ValueError("Mounting hole depth must be positive")
    if chamfer <= 0 or chamfer >= hole_depth:
        raise ValueError("Mounting hole chamfer must be smaller than its depth")
    if chamfer_radius >= housing.spacer_diameter / 2:
        raise ValueError("Mounting hole chamfer leaves no spacer wall")

    straight_hole = make_cylinder(
        centre=centre,
        diameter=mount.diameter,
        height=hole_depth + BOOLEAN_OVERLAP,
        bottom_z=hole_bottom_z,
    )
    entry_chamfer = Pos(
        centre.x,
        centre.y,
        hole_top_z - chamfer,
    ) * Cone(
        hole_radius,
        chamfer_radius + BOOLEAN_OVERLAP,
        chamfer + BOOLEAN_OVERLAP,
        align=(Align.CENTER, Align.CENTER, Align.MIN),
    )
    return straight_hole + entry_chamfer


def hexagon_radius(vent: VentConfig) -> float:
    """Convert a hexagon's flat-to-flat width to its circumradius."""
    return vent.hexagon_flat_to_flat / sqrt(3)


def vent_bounds(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
) -> tuple[float, float, float, float]:
    """Return min/max X and Y for ventilation below the PCB."""
    origin = pcb_origin(housing)
    return (
        origin.x + vent.edge_clearance,
        origin.x + pcb.width - vent.edge_clearance,
        origin.y + vent.edge_clearance,
        origin.y + pcb.length - vent.edge_clearance,
    )


def is_clear_of_mounts(
    centre: Point,
    mount_centres: tuple[Point, ...],
    minimum_distance: float,
) -> bool:
    """Check whether a vent centre has enough distance from every mount."""
    minimum_distance_squared = minimum_distance**2
    return all(
        squared_distance(centre, mount_centre) >= minimum_distance_squared
        for mount_centre in mount_centres
    )


def calculate_hex_centres(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
) -> tuple[Point, ...]:
    """Calculate honeycomb centres below the PCB."""
    bounds = vent_bounds(pcb, housing, vent)
    mount_centres = tuple(
        to_housing_coordinates(mount.position, housing)
        for mount in pcb.mounting_holes
    )
    minimum_mount_distance = vent.mount_clearance + hexagon_radius(vent)
    return calculate_hex_centres_in_bounds(
        bounds,
        vent,
        excluded_centres=mount_centres,
        minimum_exclusion_distance=minimum_mount_distance,
    )


def calculate_hex_centres_in_bounds(
    bounds: tuple[float, float, float, float],
    vent: VentConfig,
    excluded_centres: tuple[Point, ...] = (),
    minimum_exclusion_distance: float = 0.0,
) -> tuple[Point, ...]:
    """Calculate staggered honeycomb centres inside rectangular bounds."""
    radius = hexagon_radius(vent)
    pitch_x = vent.hexagon_flat_to_flat + vent.web_width
    pitch_y = 1.5 * radius + vent.web_width
    min_x, max_x, min_y, max_y = bounds

    centres: list[Point] = []
    row_index = 0
    y = min_y + radius

    while y + radius <= max_y:
        row_offset = pitch_x / 2 if row_index % 2 else 0.0
        x = min_x + vent.hexagon_flat_to_flat / 2 + row_offset

        while x + vent.hexagon_flat_to_flat / 2 <= max_x:
            centre = Point(x, y)
            if is_clear_of_mounts(
                centre,
                excluded_centres,
                minimum_exclusion_distance,
            ):
                centres.append(centre)
            x += pitch_x

        y += pitch_y
        row_index += 1

    return tuple(centres)


def cable_support_size(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
) -> tuple[float, float]:
    """Calculate width and length of the cable support."""
    width = housing.margin_west - cable_support.edge_inset
    width -= cable_support.pcb_clearance
    length = pcb.length + housing.margin_south + housing.margin_north
    length -= cable_support.edge_inset
    return width, length


def calculate_cable_slot_centres(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
) -> tuple[Point, ...]:
    """Distribute cable-tie slots evenly along the cable support."""
    support_width, support_length = cable_support_size(
        pcb,
        housing,
        cable_support,
    )
    centre_x = cable_support.edge_inset + support_width / 2
    spacing_y = support_length / (cable_support.slot_count + 1)
    return tuple(
        Point(
            centre_x,
            cable_support.edge_inset + index * spacing_y,
        )
        for index in range(1, cable_support.slot_count + 1)
    )


def pair_slot_centres(
    centres: tuple[Point, ...],
) -> tuple[tuple[Point, Point], ...]:
    """Group consecutive slot centres into cable-tie pairs."""
    if len(centres) % 2:
        raise ValueError("Cable slot count must be even to create recesses")
    return tuple(zip(centres[::2], centres[1::2]))


def adjacent_slot_centres(
    centres: tuple[Point, ...],
) -> tuple[tuple[Point, Point], ...]:
    """Return every pair of neighbouring cable-slot centres."""
    return tuple(zip(centres, centres[1:]))


def calculate_cable_wall_hole_centres(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
) -> tuple[WallHoleCentre, ...]:
    """Return wall-hole centres between each pair of cable slots."""
    slot_centres = calculate_cable_slot_centres(pcb, housing, cable_support)
    support_top_z = housing.base_thickness + housing.spacer_height
    centre_z = (
        support_top_z
        + cable_support.wall_hole_clearance
        + cable_support.wall_hole_diameter / 2
        + cable_support.wall_hole_chamfer
    )
    return tuple(
        WallHoleCentre(
            y=(first.y + second.y) / 2,
            z=centre_z,
        )
        for first, second in adjacent_slot_centres(slot_centres)
    )


def calculate_snap_positions(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
) -> tuple[SnapPosition, ...]:
    """Place two snaps each on the north and south walls."""
    outer_width, _ = housing_footprint(pcb, housing)
    if any(not 0 < fraction < 1 for fraction in lid.snap_x_fractions):
        raise ValueError("Snap X fractions must lie between zero and one")

    positions: list[SnapPosition] = []
    for fraction in lid.snap_x_fractions:
        x = outer_width * fraction
        positions.extend(
            (
                SnapPosition(x, "south"),
                SnapPosition(x, "north"),
            )
        )
    return tuple(positions)


def lid_vent_bounds(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
) -> tuple[float, float, float, float]:
    """Return the vented area while retaining a frame and solid west section."""
    width, length = housing_footprint(pcb, housing)
    clearance = lid.vent_edge_clearance
    if not 0 <= lid.closed_west_fraction < 1:
        raise ValueError("Closed west fraction must lie from zero up to one")
    vent_min_x = max(clearance, width * lid.closed_west_fraction)
    return vent_min_x, width - clearance, clearance, length - clearance


def button_position(
    housing: HousingConfig,
    button: ButtonActuatorConfig,
) -> Point:
    """Return the PCB button position in housing coordinates."""
    return to_housing_coordinates(button.pcb_position, housing)


def button_flexure_bounds(
    housing: HousingConfig,
    button: ButtonActuatorConfig,
) -> tuple[float, float, float, float]:
    """Return min/max X and Y of the integrated button flexure."""
    centre = button_position(housing, button)
    free_edge_y = centre.y - button.pin_offset_from_free_edge
    return (
        centre.x - button.flexure_width / 2,
        centre.x + button.flexure_width / 2,
        free_edge_y,
        free_edge_y + button.flexure_length,
    )


def is_outside_button_keepout(
    centre: Point,
    housing: HousingConfig,
    vent: VentConfig,
    button: ButtonActuatorConfig,
) -> bool:
    """Keep lid vents clear of the flexible button and its cutting slots."""
    min_x, max_x, min_y, max_y = button_flexure_bounds(housing, button)
    clearance = hexagon_radius(vent) + button.vent_keepout
    return not (
        min_x - clearance <= centre.x <= max_x + clearance
        and min_y - clearance <= centre.y <= max_y + clearance
    )


def calculate_lid_hex_centres(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
    lid: LidConfig,
    button: ButtonActuatorConfig,
) -> tuple[Point, ...]:
    """Calculate the honeycomb pattern outside the solid west section."""
    centres = calculate_hex_centres_in_bounds(
        lid_vent_bounds(pcb, housing, lid),
        vent,
    )
    return tuple(
        centre
        for centre in centres
        if is_outside_button_keepout(centre, housing, vent, button)
    )


# Model construction


def create_hex_cut(
    centres: tuple[Point, ...],
    vent: VentConfig,
    bottom_z: float,
    height: float,
):
    """Create a group of hexagonal cutters at a specified Z range."""
    locations = tuple((centre.x, centre.y) for centre in centres)
    if not locations:
        raise ValueError("Hex pattern contains no openings")

    with BuildPart() as hex_cut:
        with BuildSketch(Plane.XY.offset(bottom_z)):
            with Locations(*locations):
                RegularPolygon(
                    hexagon_radius(vent),
                    HEXAGON_SIDE_COUNT,
                    rotation=HEXAGON_ROTATION_DEGREES,
                )
        extrude(amount=height)

    return hex_cut.part


def create_housing_walls(pcb: PcbConfig, housing: HousingConfig):
    """Create open-top perimeter walls on the housing base."""
    outer_width, outer_length = housing_footprint(pcb, housing)
    inner_width = outer_width - 2 * housing.wall_thickness
    inner_length = outer_length - 2 * housing.wall_thickness

    if inner_width <= 0 or inner_length <= 0:
        raise ValueError("Housing wall thickness leaves no inner cavity")

    outer_walls = Pos(0, 0, housing.base_thickness) * Box(
        outer_width,
        outer_length,
        housing.wall_height,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    inner_cavity = Pos(
        housing.wall_thickness,
        housing.wall_thickness,
        housing.base_thickness - BOOLEAN_OVERLAP,
    ) * Box(
        inner_width,
        inner_length,
        housing.wall_height + 2 * BOOLEAN_OVERLAP,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    return outer_walls - inner_cavity


def create_hex_vent_cut(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
):
    """Create all hexagonal openings as one cutting body."""
    centres = calculate_hex_centres(pcb, housing, vent)
    return create_hex_cut(
        centres,
        vent,
        bottom_z=-BOOLEAN_OVERLAP,
        height=housing.base_thickness + 2 * BOOLEAN_OVERLAP,
    )


def snap_tab_min_y(
    position: SnapPosition,
    outer_length: float,
    housing: HousingConfig,
    lid: LidConfig,
) -> float:
    """Return the lower Y coordinate of a north or south snap tab."""
    if position.side == "south":
        return housing.wall_thickness + lid.fit_clearance
    if position.side == "north":
        return (
            outer_length
            - housing.wall_thickness
            - lid.fit_clearance
            - lid.snap_tab_thickness
        )
    raise ValueError(f"Unsupported snap side: {position.side}")


def create_snap_hook(
    position: SnapPosition,
    tab_min_y: float,
    lid: LidConfig,
):
    """Create a tapered hook that flexes inward during lid insertion."""
    hook_bottom = lid.snap_hook_bottom_offset
    hook_top = hook_bottom + lid.snap_hook_height

    if position.side == "south":
        wall_facing_y = tab_min_y
        profile = (
            (wall_facing_y, hook_bottom),
            (wall_facing_y, hook_top),
            (wall_facing_y - lid.snap_hook_depth, hook_top),
        )
    elif position.side == "north":
        wall_facing_y = tab_min_y + lid.snap_tab_thickness
        profile = (
            (wall_facing_y, hook_bottom),
            (wall_facing_y, hook_top),
            (wall_facing_y + lid.snap_hook_depth, hook_top),
        )
    else:
        raise ValueError(f"Unsupported snap side: {position.side}")

    with BuildPart() as hook:
        with BuildSketch(
            Plane.YZ.offset(position.x - lid.snap_tab_width / 2)
        ):
            Polygon(*profile)
        extrude(amount=lid.snap_tab_width)
    return hook.part


def create_lid_snap_tabs(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
):
    """Create four flexible snap tabs for the lid underside."""
    _, outer_length = housing_footprint(pcb, housing)
    tabs = None

    for position in calculate_snap_positions(pcb, housing, lid):
        tab_min_y = snap_tab_min_y(position, outer_length, housing, lid)
        tab = Pos(
            position.x - lid.snap_tab_width / 2,
            tab_min_y,
            0,
        ) * Box(
            lid.snap_tab_width,
            lid.snap_tab_thickness,
            lid.snap_tab_length + BOOLEAN_OVERLAP,
            align=(Align.MIN, Align.MIN, Align.MIN),
        )
        tab += create_snap_hook(position, tab_min_y, lid)
        tabs = tab if tabs is None else tabs + tab

    return tabs


def create_lid_guide(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
):
    """Create an internal guide rim with reliefs around flexible snap tabs."""
    width, length = housing_footprint(pcb, housing)
    outer_inset = housing.wall_thickness + lid.fit_clearance
    guide_outer_width = width - 2 * outer_inset
    guide_outer_length = length - 2 * outer_inset
    guide_inner_width = guide_outer_width - 2 * lid.guide_thickness
    guide_inner_length = guide_outer_length - 2 * lid.guide_thickness
    guide_bottom_z = lid.snap_tab_length - lid.guide_depth

    if lid.guide_depth <= 0 or lid.guide_depth > lid.snap_tab_length:
        raise ValueError("Lid guide depth must fit within the snap-tab length")
    if guide_inner_width <= 0 or guide_inner_length <= 0:
        raise ValueError("Lid guide thickness leaves no inner opening")

    guide_outer = Pos(
        outer_inset,
        outer_inset,
        guide_bottom_z,
    ) * Box(
        guide_outer_width,
        guide_outer_length,
        lid.guide_depth + BOOLEAN_OVERLAP,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    guide_inner = Pos(
        outer_inset + lid.guide_thickness,
        outer_inset + lid.guide_thickness,
        guide_bottom_z - BOOLEAN_OVERLAP,
    ) * Box(
        guide_inner_width,
        guide_inner_length,
        lid.guide_depth + 2 * BOOLEAN_OVERLAP,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    guide = guide_outer - guide_inner

    relief_width = lid.snap_tab_width + 2 * lid.guide_snap_clearance
    for position in calculate_snap_positions(pcb, housing, lid):
        if position.side == "south":
            relief_min_y = outer_inset - BOOLEAN_OVERLAP
        elif position.side == "north":
            relief_min_y = length - outer_inset - lid.guide_thickness
            relief_min_y -= BOOLEAN_OVERLAP
        else:
            raise ValueError(f"Unsupported snap side: {position.side}")

        relief = Pos(
            position.x - relief_width / 2,
            relief_min_y,
            guide_bottom_z - BOOLEAN_OVERLAP,
        ) * Box(
            relief_width,
            lid.guide_thickness + 2 * BOOLEAN_OVERLAP,
            lid.guide_depth + 2 * BOOLEAN_OVERLAP,
            align=(Align.MIN, Align.MIN, Align.MIN),
        )
        guide -= relief

    return guide


def create_snap_receiver_cut(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
):
    """Create through-slots for latching and externally releasing the snaps."""
    _, outer_length = housing_footprint(pcb, housing)
    wall_top_z = housing.base_thickness + housing.wall_height
    hook_bottom_z = (
        wall_top_z
        - lid.snap_tab_length
        + lid.snap_hook_bottom_offset
    )
    pocket_bottom_z = hook_bottom_z - lid.receiver_clearance
    pocket_height = lid.snap_hook_height + 2 * lid.receiver_clearance
    pocket_width = lid.snap_tab_width + 2 * lid.receiver_clearance
    receiver_cut = None

    for position in calculate_snap_positions(pcb, housing, lid):
        if position.side == "south":
            pocket_min_y = -BOOLEAN_OVERLAP
        elif position.side == "north":
            pocket_min_y = outer_length - housing.wall_thickness
            pocket_min_y -= BOOLEAN_OVERLAP
        else:
            raise ValueError(f"Unsupported snap side: {position.side}")

        pocket = Pos(
            position.x - pocket_width / 2,
            pocket_min_y,
            pocket_bottom_z,
        ) * Box(
            pocket_width,
            housing.wall_thickness + 2 * BOOLEAN_OVERLAP,
            pocket_height,
            align=(Align.MIN, Align.MIN, Align.MIN),
        )
        receiver_cut = pocket if receiver_cut is None else receiver_cut + pocket

    return receiver_cut


def create_button_flexure_cut(
    housing: HousingConfig,
    lid: LidConfig,
    button: ButtonActuatorConfig,
):
    """Create U-shaped isolation slots and thin the integrated button spring."""
    min_x, max_x, min_y, max_y = button_flexure_bounds(housing, button)
    slot_width = button.flexure_slot_width
    plate_bottom_z = lid.snap_tab_length
    through_height = lid.thickness + 2 * BOOLEAN_OVERLAP
    recess_height = lid.thickness - button.flexure_thickness

    if recess_height <= 0:
        raise ValueError("Button flexure must be thinner than the lid")
    if button.travel <= 0:
        raise ValueError("Button travel must be positive")

    left_slot = Pos(
        min_x - slot_width,
        min_y,
        plate_bottom_z - BOOLEAN_OVERLAP,
    ) * Box(
        slot_width,
        max_y - min_y,
        through_height,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    right_slot = Pos(
        max_x,
        min_y,
        plate_bottom_z - BOOLEAN_OVERLAP,
    ) * Box(
        slot_width,
        max_y - min_y,
        through_height,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    free_end_slot = Pos(
        min_x - slot_width,
        min_y - slot_width,
        plate_bottom_z - BOOLEAN_OVERLAP,
    ) * Box(
        max_x - min_x + 2 * slot_width,
        slot_width,
        through_height,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    underside_recess = Pos(
        min_x,
        min_y,
        plate_bottom_z - BOOLEAN_OVERLAP,
    ) * Box(
        max_x - min_x,
        max_y - min_y,
        recess_height + BOOLEAN_OVERLAP,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    return left_slot + right_slot + free_end_slot + underside_recess


def create_button_pin(
    housing: HousingConfig,
    lid: LidConfig,
    button: ButtonActuatorConfig,
):
    """Create the actuator pin from the flexure down to the PCB button."""
    centre = button_position(housing, button)
    wall_top_z = housing.base_thickness + housing.wall_height
    lid_assembly_offset_z = wall_top_z - lid.snap_tab_length
    button_surface_z = (
        housing.base_thickness
        + housing.spacer_height
        + button.height_above_spacers
    )
    pin_tip_z = (
        button_surface_z
        + button.pin_tip_clearance
        - lid_assembly_offset_z
    )
    flexure_underside_z = (
        lid.snap_tab_length
        + lid.thickness
        - button.flexure_thickness
    )
    pin_height = flexure_underside_z - pin_tip_z + BOOLEAN_OVERLAP

    if pin_height <= 0:
        raise ValueError("Button pin does not reach the PCB button")

    return Pos(centre.x, centre.y, pin_tip_z) * Cylinder(
        button.pin_diameter / 2,
        pin_height,
        align=(Align.CENTER, Align.CENTER, Align.MIN),
    )


def create_lid_label_cuts(
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
    label: LidLabelConfig,
):
    """Create lengthwise stencil-letter cutters for the solid west section."""
    width, length = housing_footprint(pcb, housing)
    plate_bottom_z = lid.snap_tab_length
    cut_height = lid.thickness + 2 * BOOLEAN_OVERLAP
    font_path = Path(__file__).parent / label.font_path

    if not font_path.is_file():
        raise FileNotFoundError(f"Lid label font not found: {font_path}")

    with BuildPart() as label_cut:
        with BuildSketch(Plane.XY.offset(plate_bottom_z - BOOLEAN_OVERLAP)):
            Text(
                label.text,
                label.font_size,
                font_path=font_path,
            )
        extrude(amount=cut_height)

    label_centre_x = width * lid.closed_west_fraction / 2
    label_centre_y = length / 2
    placed_cut = (
        Pos(label_centre_x, label_centre_y, 0)
        * Rot(0, 0, 90)
        * label_cut.part
    )
    return tuple(placed_cut.solids())


def create_lid_model(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
    lid: LidConfig,
    button: ButtonActuatorConfig,
    label: LidLabelConfig,
):
    """Create a ventilated snap-fit lid as a separate printable model."""
    width, length = housing_footprint(pcb, housing)
    plate_bottom_z = lid.snap_tab_length
    lid_body = Pos(0, 0, plate_bottom_z) * Box(
        width,
        length,
        lid.thickness,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )
    lid_body += create_lid_snap_tabs(pcb, housing, lid)
    lid_body += create_lid_guide(pcb, housing, lid)

    vent_centres = calculate_lid_hex_centres(
        pcb,
        housing,
        vent,
        lid,
        button,
    )
    lid_body -= create_hex_cut(
        vent_centres,
        vent,
        bottom_z=plate_bottom_z - BOOLEAN_OVERLAP,
        height=lid.thickness + 2 * BOOLEAN_OVERLAP,
    )
    lid_body -= create_button_flexure_cut(housing, lid, button)
    lid_body += create_button_pin(housing, lid, button)
    with SkipClean():
        lid_body = lid_body.cut(
            *create_lid_label_cuts(pcb, housing, lid, label)
        )
    return lid_body


def orient_lid_for_print(
    lid_model,
    pcb: PcbConfig,
    housing: HousingConfig,
    lid: LidConfig,
):
    """Place the lid exterior-down with snap tabs pointing upward."""
    _, length = housing_footprint(pcb, housing)
    total_height = lid.snap_tab_length + lid.thickness
    return Pos(0, length, total_height) * Rot(180, 0, 0) * lid_model


def create_cable_support(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
):
    """Create the raised cable support on top of the housing base."""
    width, length = cable_support_size(pcb, housing, cable_support)
    return Pos(
        cable_support.edge_inset,
        cable_support.edge_inset,
        housing.base_thickness,
    ) * Box(
        width,
        length,
        housing.spacer_height,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )


def create_cable_slot_cut(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
):
    """Create cable-tie slots cutting through support and housing base."""
    centres = calculate_cable_slot_centres(pcb, housing, cable_support)
    locations = tuple((centre.x, centre.y) for centre in centres)
    total_height = housing.base_thickness + housing.spacer_height

    with BuildPart() as slot_cut:
        with BuildSketch(Plane.XY.offset(-BOOLEAN_OVERLAP)):
            with Locations(*locations):
                SlotOverall(
                    cable_support.slot_length,
                    cable_support.slot_width,
                )
        extrude(amount=total_height + 2 * BOOLEAN_OVERLAP)

    return slot_cut.part


def create_cable_recess_cut(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
):
    """Create underside pockets so installed cable ties remain recessed."""
    total_height = housing.base_thickness + housing.spacer_height
    if cable_support.underside_recess_depth >= total_height:
        raise ValueError("Cable recess must leave material at the top")

    centres = calculate_cable_slot_centres(pcb, housing, cable_support)
    centre_pairs = pair_slot_centres(centres)
    recess_cut = None

    for first, second in centre_pairs:
        pocket_min_x = first.x - cable_support.slot_length / 2
        pocket_min_y = first.y - cable_support.slot_width / 2
        pocket_length = second.y - first.y + cable_support.slot_width
        pocket = Pos(
            pocket_min_x,
            pocket_min_y,
            -BOOLEAN_OVERLAP,
        ) * Box(
            cable_support.slot_length,
            pocket_length,
            cable_support.underside_recess_depth + BOOLEAN_OVERLAP,
            align=(Align.MIN, Align.MIN, Align.MIN),
        )
        recess_cut = pocket if recess_cut is None else recess_cut + pocket

    return recess_cut


def create_cable_wall_hole_cut(
    pcb: PcbConfig,
    housing: HousingConfig,
    cable_support: CableSupportConfig,
):
    """Create horizontal cable holes through the left housing wall."""
    centres = calculate_cable_wall_hole_centres(pcb, housing, cable_support)
    wall_top_z = housing.base_thickness + housing.wall_height
    hole_radius = cable_support.wall_hole_diameter / 2
    chamfer = cable_support.wall_hole_chamfer
    chamfer_radius = hole_radius + chamfer

    if chamfer <= 0 or 2 * chamfer >= housing.wall_thickness:
        raise ValueError("Cable hole chamfer must be smaller than half the wall")
    if any(centre.z + chamfer_radius > wall_top_z for centre in centres):
        raise ValueError("Cable wall holes extend above the housing wall")

    hole_cut = None
    for centre in centres:
        through_hole = Pos(
            -BOOLEAN_OVERLAP,
            centre.y,
            centre.z,
        ) * Rot(0, 90, 0) * Cylinder(
            hole_radius,
            housing.wall_thickness + 2 * BOOLEAN_OVERLAP,
            align=(Align.CENTER, Align.CENTER, Align.MIN),
        )
        outer_chamfer = Pos(
            0,
            centre.y,
            centre.z,
        ) * Rot(0, 90, 0) * Cone(
            chamfer_radius,
            hole_radius,
            chamfer,
            align=(Align.CENTER, Align.CENTER, Align.MIN),
        )
        inner_chamfer = Pos(
            housing.wall_thickness - chamfer,
            centre.y,
            centre.z,
        ) * Rot(0, 90, 0) * Cone(
            hole_radius,
            chamfer_radius,
            chamfer,
            align=(Align.CENTER, Align.CENTER, Align.MIN),
        )
        hole = through_hole + outer_chamfer + inner_chamfer
        hole_cut = hole if hole_cut is None else hole_cut + hole

    return hole_cut


def create_housing_model(
    pcb: PcbConfig,
    housing: HousingConfig,
    vent: VentConfig,
    cable_support: CableSupportConfig,
    lid: LidConfig,
):
    """Create the complete housing base."""
    width, length = housing_footprint(pcb, housing)
    housing_body = Box(
        width,
        length,
        housing.base_thickness,
        align=(Align.MIN, Align.MIN, Align.MIN),
    )

    mounts = all_mounting_holes(pcb, housing)
    housing_body += create_housing_walls(pcb, housing)

    for mount in mounts:
        housing_body += make_spacer(mount, housing)

    housing_body += create_cable_support(pcb, housing, cable_support)
    housing_body -= create_hex_vent_cut(pcb, housing, vent)

    for mount in mounts:
        housing_body -= make_mounting_hole_cut(mount, housing)

    housing_body -= create_cable_slot_cut(pcb, housing, cable_support)
    housing_body -= create_cable_recess_cut(pcb, housing, cable_support)
    housing_body -= create_cable_wall_hole_cut(pcb, housing, cable_support)
    housing_body -= create_snap_receiver_cut(pcb, housing, lid)

    return housing_body


def export_model(model, export: ExportConfig, file_name: str) -> Path:
    """Export a model as STL and return its path."""
    output_directory = Path(__file__).with_name(export.directory_name)
    output_directory.mkdir(exist_ok=True)
    output_path = output_directory / f"{file_name}.stl"
    export_stl(model, output_path)
    return output_path


def main() -> None:
    housing_model = create_housing_model(
        PCB_CONFIG,
        HOUSING_CONFIG,
        VENT_CONFIG,
        CABLE_SUPPORT_CONFIG,
        LID_CONFIG,
    )
    lid_model = create_lid_model(
        PCB_CONFIG,
        HOUSING_CONFIG,
        LID_VENT_CONFIG,
        LID_CONFIG,
        BUTTON_ACTUATOR_CONFIG,
        LID_LABEL_CONFIG,
    )
    print_ready_lid = orient_lid_for_print(
        lid_model,
        PCB_CONFIG,
        HOUSING_CONFIG,
        LID_CONFIG,
    )
    housing_path = export_model(
        housing_model,
        EXPORT_CONFIG,
        EXPORT_CONFIG.housing_file_name,
    )
    lid_path = export_model(
        print_ready_lid,
        EXPORT_CONFIG,
        EXPORT_CONFIG.lid_file_name,
    )
    print(f"Created housing: {housing_path}")
    print(f"Created lid: {lid_path}")


if __name__ == "__main__":
    main()
