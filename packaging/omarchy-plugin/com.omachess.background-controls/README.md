# Omachess Background Controls

This is an explicitly enabled, on-demand Omarchy bar-widget plugin. It reads
the `com.omachess.Omachess.BackgroundJobs` D-Bus service through the packaged
`omachess-background-control` helper. The worker owns job execution, durable
state, checkpoints, and the advertised control list; this widget is only a
desktop control surface.

The manifest targets Omarchy 4 and the version-1 bar-widget schema. The
explicit enable helper checks the installed Omarchy major before copying the
plugin into the user's plugin directory.

The package installs this source under `/usr/share/omachess/omarchy-plugin`.
Run `omachess-background-controls-enable` once to copy it into the user's
Omarchy plugin directory and add it to the bar. Package installation never
edits the user's shell configuration.
