function y = db2pow(ydB)
% Octave compatibility (MATLAB Signal Processing Toolbox): power ratio from dB
y = 10.^(ydB/10);
end
