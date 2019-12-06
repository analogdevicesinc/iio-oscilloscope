#!/usr/bin/perl

# The problem:
# GtkComboBoxText has a bug that makes it unsuable (can't get the active text,
# or insert new text, etc.). In order to work, instances of GtkComboBoxText
# need the property "entry-text-column" >= 0, which doesn't happen when a
# GtkComboBoxText is obtained from a glade file (creating them with new works).

# The solution:
# This script adds the missing property of GtkComboBoxText objects that are
# described in .glade file.

# Usage:
# ./fixup-comboboxtext.pl path_to_glade_file 

# Note: this script can be passed to fixup-all-glades.sh to go through all
# glade files in this repo.

use strict;

my $fileToFix = $ARGV[0];
my $tmpFileToRead = $fileToFix.'.bak';

rename($fileToFix, $tmpFileToRead) || die "Cannot rename file $fileToFix: $!";
open my $inFile, '<', $tmpFileToRead or die "Can't read $tmpFileToRead: $!";
open my $outFile, '>', $fileToFix or die "Can't write to file $fileToFix: $!";

my $comboBoxTextDetected = 0;
my $entryTextColumnDetected = 0;

while( <$inFile> ) {
        # Check if we're at a GtkComboBoxText definition.
        if ($_ =~ m/<object class="GtkComboBoxText"/)
        {
                $comboBoxTextDetected = 1;
        }

        # Check if the GtkComboBoxText definition has property "entry_text_column" defined.
        if ($comboBoxTextDetected && $_ =~ m/<property name="entry_text_column">/)
        {
                $entryTextColumnDetected = 1;
        }

        # We've reach the end of the object definition or the start if items definition.
        # Add the "entry_text_column" is not already there. Reset things.
        if ($comboBoxTextDetected && ($_ =~ m/<\/object>/ || $_ =~ m/<items>/))
        {
                # Find indentation for </object>
                $_ =~ m/(\s+)(<.+>)/;
                my $indentation = $1;

                # Increase indentation with 2 spaces
                if ($2 eq "</object>")
                {
                        $indentation .="  ";
                }

                if (!$entryTextColumnDetected)
                {
                    print $outFile $indentation;
                    print $outFile "<property name=\"entry_text_column\">0</property>\n";
                }

                $comboBoxTextDetected = 0;
                $entryTextColumnDetected = 0;
        }

        print $outFile $_;
}

close $inFile;
close $outFile;
unlink($tmpFileToRead) || die "Cannont unlink $tmpFileToRead: $!";
