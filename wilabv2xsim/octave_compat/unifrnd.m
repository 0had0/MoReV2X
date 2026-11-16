function r = unifrnd(a, b, varargin)
% Octave compatibility (MATLAB Statistics Toolbox): uniform random numbers in [a, b]
if isempty(varargin)
    sz = size(a + b);
else
    sz = [varargin{:}];
    if numel(sz) == 1
        sz = [sz sz];
    end
end
r = a + (b - a) .* rand(sz);
end
