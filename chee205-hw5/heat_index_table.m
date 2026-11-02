%% CHEE 205 - Homework 5 (Optional "Diving Deeper"): Heat Index Table and Surface Plot
% This script runs the same WPC heat index calculation used in heat_index.m
% for many temperature / relative humidity combinations, then:
%   1. Writes an output file with temperature, relative humidity, heat index
%      (before adjustment), adjustment factor, and final result.
%   2. Makes a 3-D surface plot of heat index vs. temperature and humidity.
%
% Temperatures:      35 to 115 deg F in 10 deg F steps   (9 values)
% Relative humidity: 5 to 100 %   in 5 % steps           (20 values)
%
% Output files (saved in the current MATLAB folder):
%   heat_index_results.csv   - plain text table (opens in Excel)
%   heat_index_results.xlsx  - Excel workbook (MATLAB only, uses writetable)
%   heat_index_surface.png   - picture of the surface plot

clear; clc; close all;

%% Set up the temperature and relative humidity values to test
T_list  = 35:10:115;    % temperature, deg F
RH_list = 5:5:100;      % relative humidity, percent

nT  = length(T_list);
nRH = length(RH_list);

% Pre-allocate matrices: rows = humidity, columns = temperature
HI_raw   = zeros(nRH, nT);   % heat index before adjustment, deg F
HI_adj   = zeros(nRH, nT);   % adjustment factor, deg F
HI_final = zeros(nRH, nT);   % final heat index, deg F
method   = cell(nRH, nT);    % which equation was used

%% Loop over every temperature / humidity combination
for i = 1:nRH
    for j = 1:nT
        T  = T_list(j);
        RH = RH_list(i);

        % Simple (Steadman) formula first
        HI_simple  = 0.5 * (T + 61.0 + (T - 68.0) * 1.2 + RH * 0.094);
        adjustment = 0;

        if (HI_simple + T) / 2 < 80
            HI = HI_simple;
            method{i, j} = 'Simple';
        else
            % Full Rothfusz regression
            HI = -42.379 + 2.04901523*T + 10.14333127*RH ...
                 - 0.22475541*T*RH - 0.00683783*T^2 ...
                 - 0.05481717*RH^2 + 0.00122874*T^2*RH ...
                 + 0.00085282*T*RH^2 - 0.00000199*T^2*RH^2;
            method{i, j} = 'Rothfusz';

            % Adjustment factors
            if RH < 13 && T >= 80 && T <= 112
                adjustment = -((13 - RH) / 4) * sqrt((17 - abs(T - 95)) / 17);
                method{i, j} = 'Rothfusz + low-RH adj';
            elseif RH > 85 && T >= 80 && T <= 87
                adjustment = ((RH - 85) / 10) * ((87 - T) / 5);
                method{i, j} = 'Rothfusz + high-RH adj';
            end
        end

        HI_raw(i, j)   = HI;
        HI_adj(i, j)   = adjustment;
        HI_final(i, j) = HI + adjustment;
    end
end

HI_final_C = (HI_final - 32) * 5/9;   % final heat index in deg C

%% Build a long-format table: one row per temperature / humidity pair
[T_grid, RH_grid] = meshgrid(T_list, RH_list);
T_col   = reshape(T_grid',   [], 1);
RH_col  = reshape(RH_grid',  [], 1);
raw_col = reshape(HI_raw',   [], 1);
adj_col = reshape(HI_adj',   [], 1);
fin_col = reshape(HI_final', [], 1);
finC    = reshape(HI_final_C', [], 1);
meth    = reshape(method',   [], 1);

%% Write the CSV output file (works in every MATLAB version)
fid = fopen('heat_index_results.csv', 'w');
fprintf(fid, 'Temperature_F,Relative_Humidity_pct,Heat_Index_Before_Adjustment_F,Adjustment_Factor_F,Final_Heat_Index_F,Final_Heat_Index_C,Method\n');
for k = 1:length(T_col)
    fprintf(fid, '%g,%g,%.2f,%.2f,%.2f,%.2f,%s\n', T_col(k), RH_col(k), ...
            raw_col(k), adj_col(k), fin_col(k), finC(k), meth{k});
end
fclose(fid);
disp('Saved heat_index_results.csv');

%% Also write an Excel file if writetable is available (MATLAB R2013b+)
if exist('writetable', 'file') || exist('writetable', 'builtin')
    results = table(T_col, RH_col, round(raw_col, 2), round(adj_col, 2), ...
                    round(fin_col, 2), round(finC, 2), meth, ...
        'VariableNames', {'Temperature_F', 'Relative_Humidity_pct', ...
                          'Heat_Index_Before_Adjustment_F', 'Adjustment_Factor_F', ...
                          'Final_Heat_Index_F', 'Final_Heat_Index_C', 'Method'});
    writetable(results, 'heat_index_results.xlsx');
    disp('Saved heat_index_results.xlsx');
end

%% 3-D surface plot of the final heat index
figure;
surf(T_grid, RH_grid, HI_final);
xlabel('Air Temperature (\circF)');
ylabel('Relative Humidity (%)');
zlabel('Heat Index (\circF)');
title('Heat Index vs. Temperature and Relative Humidity (WPC Equation)');
colorbar;
view(3);        % standard 3-D view
grid on;
saveas(gcf, 'heat_index_surface.png');
disp('Saved heat_index_surface.png');
