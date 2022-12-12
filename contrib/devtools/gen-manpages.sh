#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

CLORE_BLOCKCHAIND=${CLORE_BLOCKCHAIND:-$SRCDIR/clore_blockchaind}
CLORECLI=${CLORECLI:-$SRCDIR/clore-cli}
CLORETX=${CLORETX:-$SRCDIR/clore-tx}
CLOREQT=${CLOREQT:-$SRCDIR/qt/clore-qt}

[ ! -x $CLORE_BLOCKCHAIND ] && echo "$CLORE_BLOCKCHAIND not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
CLOREVER=($($CLORECLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for clore_blockchaind if --version-string is not set,
# but has different outcomes for clore-qt and clore-cli.
echo "[COPYRIGHT]" > footer.h2m
$CLORE_BLOCKCHAIND --version | sed -n '1!p' >> footer.h2m

for cmd in $CLORE_BLOCKCHAIND $CLORECLI $CLORETX $CLOREQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${CLOREVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${CLOREVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
