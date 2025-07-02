# Utility script/command for gdb to show how much of stack is each function eating
# Usage: `source utils/gdb/gdb_stack_offsets.py` in the gdb console

import gdb


def frame_address(frame):
    return int(frame.read_register("sp").cast(gdb.lookup_type("uint64_t")))


def invoke():
    bottommost_frame = gdb.selected_frame()
    while bottommost_frame.older():
        bottommost_frame = bottommost_frame.older()
    stack_bottom = frame_address(bottommost_frame)

    frame = gdb.selected_frame()
    prev_offset = stack_bottom - frame_address(frame)
    while frame:
        # Calculate the offset relative to the bottommost frame's base address
        offset = stack_bottom - frame_address(frame)

        # f"{frame.level():>2}  {offset:>4}  {offset_str:>4}  {name:<64}"
        txt = "{lvl:>4} {offset:>4} {rel_offset:>4} {func:<64}".format(
            lvl=frame.level(),
            offset=offset,
            rel_offset="+" + str(prev_offset - offset),
            func=(frame.name() or "<unknown>")[:63],
        )

        # Add source file and line if available
        sal = frame.find_sal()
        if sal and sal.symtab and sal.line:
            txt += f" at {sal.symtab.filename}:{sal.line}"

        print(txt)

        # Move to the calling frame
        frame = frame.older()
        prev_offset = offset


invoke()
