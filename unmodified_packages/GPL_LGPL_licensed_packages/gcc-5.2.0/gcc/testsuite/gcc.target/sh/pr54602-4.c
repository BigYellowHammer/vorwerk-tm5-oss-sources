/* Verify that the delay slot is stuffed with register pop insns on SH3* and
   SH4* targets, where the stack pointer is not used by the rte insn.  If
   everything works out, we won't see a nop insn.  */
/* { dg-do compile }  */
/* { dg-options "-O1" } */
/* { dg-skip-if "" { "sh*-*-*" } { "*" } { "-m3*" "-m4*" } }  */
/* { dg-final { scan-assembler-not "rte\n\tnop" } } */

int test00 (int a, int b);

int __attribute__ ((interrupt_handler))
test01 (int a, int b, int c, int d)
{
  return test00 (a, b) + c;
}
