function y = roundDigits(x, n)
% Octave compatibility: MATLAB round(x, n) (round to n decimal digits)
f = 10.^n;
y = round(x .* f) ./ f;
end
