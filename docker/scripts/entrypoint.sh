#!/bin/bash

echo "Configuring environment"

find /etc/conf -name "*.conf" -exec sh -c '/opt/replace-vars.sh {} "/etc/apache2/sites-available/$(basename {})"' \;

echo "Enabling sites"

a2ensite apache && a2ensite upq

echo "Running $@ at $(printf '%s %s\n' "$(date)")"

exec $@
