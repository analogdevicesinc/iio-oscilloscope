#!/bin/bash
echo Fixing This $1
perl -pi -e 's/GtkGrid/GtkTable/g' $1
perl -pi -e 's/GtkBox/GtkVBox/g' $1
grep "<requires lib=" $1 && sed -i.tmp '4d' $1
