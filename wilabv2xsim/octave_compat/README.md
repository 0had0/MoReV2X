MATLAB toolbox functions WiLabV2Xsim calls that plain Octave 10.3 lacks, as small
equivalents: `db2pow`, `pow2db` (Signal Processing) and `unifrnd`, `exprnd`
(Statistics). They replace the Octave Forge `statistics` package, whose current
release needs Octave 11 and whose last Octave 10 release does not compile in the
conda-forge environment.
