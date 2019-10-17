#!/bin/bash

OLDVERSION=`grep PKG_RELEASE Makefile | cut -f 2 -d =`
OLDMAJ=`echo $OLDVERSION | cut -f 1 -d .`
OLDMIN=`echo $OLDVERSION | cut -f 2 -d .`
OLDREV=`echo $OLDVERSION | cut -f 3 -d .`

if [ $1 == "major" ]; then
	echo major release being incremented
	((OLDMAJ++))
elif [ $1 == "minor" ]; then
	echo minor release being incremented
	((OLDMIN++))
elif [ $1 == "revision" ]; then
	echo revision release being incremented
	((OLDREV++))
else
	echo "usage: $0 [major|minor|revision]"
fi

NEWVERSION=$OLDMAJ.$OLDMIN.$OLDREV
echo $NEWVERSION

for FILE in Makefile Android.mk Doxyfile src/Makefile; do
	sed -i "s/$OLDVERSION/$NEWVERSION/g" $FILE
done

