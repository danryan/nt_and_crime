/* Partial-link script that collapses gcc's split .text._ZNxxx /
 * .data._ZNxxx etc into single sections. NT firmware caps the per-plugin
 * section count; with ~25 vendor applet classes Phase 6 produced 1793
 * sections (Phase 5 baseline was 976). Merging into ~6 sections keeps
 * the firmware section walker within budget.
 */
SECTIONS
{
  .text : { *(.text .text.* .gnu.linkonce.t.*) }
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
  .data : { *(.data .data.* .gnu.linkonce.d.*) }
  .data.rel.ro : { *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) }
  .data.rel.local : { *(.data.rel.local .data.rel.local.* .gnu.linkonce.d.rel.local.*) }
  .bss : { *(.bss .bss.* .gnu.linkonce.b.*) }
  .init_array : { *(.init_array .init_array.*) }
  .fini_array : { *(.fini_array .fini_array.*) }
}
