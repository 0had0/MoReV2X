function r = exprnd(mu, varargin)
% Octave compatibility (MATLAB Statistics Toolbox): exponential random numbers of mean mu
if isempty(varargin)
    sz = size(mu);
else
    sz = [varargin{:}];
    if numel(sz) == 1
        sz = [sz sz];
    end
end
r = -mu .* log(rand(sz));
end
