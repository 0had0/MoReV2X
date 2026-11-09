import sys
sys.path.insert(0, __import__("os").path.dirname(__file__))
from nbb import NB, SETUP

nb = NB()
nb.md(r'''
# 12. Does re-evaluation help the QoS of several priorities?

Three policies run on the same traces (notebook 10, section 5): `off`, `reeval` and
`reeval_preempt`. For every class this notebook measures what each policy changes, as **paired
differences**: the same scenario, density, condition, variant and seed with and without the
mechanism, so the mobility and the traffic are identical and only Mode 2 differs. A difference is
significant when its 95 % interval over the seeds excludes 0.

Questions:

1. How many re-evaluations and pre-emptions happen per class, and how many change a resource?
2. What do they change in QoS-PDR, collisions, DT and AoI, per priority?
3. Who pays for pre-emption: do low priorities lose what high priorities gain?
4. Which PER targets are met under each policy?
5. Can re-evaluation act on the hidden-node losses, or only on the sensing-visible ones?
''')
nb.code(SETUP + '''
import plots
s = plots.Sweep()
print(s.note())
flows = s.flows.copy()
flows["policy_label"] = flows.policy.map(plots.POLICY_LABELS)
''')

nb.md(r'''## 1. Checks and changes

A re-evaluation runs at T3 before every resource not yet announced, a pre-emption check at T3
before every announced one. A *change* is a check that re-selected the resource: that is the
**effective** re-evaluation (or pre-emption). Rates are per 1000 transmissions of the class.''')
nb.code('''
counts = flows.groupby(["scenario", "policy", "name"])[["reeval_checks", "reeval_changes", "preempt_checks", "preempt_changes", "transmissions"]].sum()
rates = counts.div(counts.transmissions, axis=0).drop(columns="transmissions") * 1000
rates["effective reeval share"] = counts.reeval_changes / counts.reeval_checks.replace(0, np.nan)
rates["effective preempt share"] = counts.preempt_changes / counts.preempt_checks.replace(0, np.nan)
rates.round(3)
''')
nb.code('''
part = flows[flows.policy != "off"]
fig, axes = plt.subplots(1, 2, figsize=(11, 3.4))
for ax, (value, label) in zip(axes, (("reeval_changes", "re-selections by re-evaluation"), ("preempt_changes", "pre-empted resources"))):
    t = part.groupby(["name", "policy"])[[value, "transmissions"]].sum()
    t = (1000 * t[value] / t.transmissions).unstack()
    t.reindex(plots.order_classes(t.index)).plot.bar(ax=ax, width=0.8)
    ax.set_ylabel("per 1000 transmissions"); ax.set_title(label); ax.tick_params(axis="x", rotation=30)
plt.tight_layout()
''')

nb.md(r'''## 2. What each mechanism changes, per priority

Paired differences against `off`. Positive Δ QoS-PDR is better, negative Δ collision rate, Δ DT and
Δ AoI are better. Classes are sorted by SL priority, most important first.''')
nb.code('''
def delta_plot(value, label, frame=flows):
    diff = plots.paired_difference(frame, value)
    if not len(diff):
        print("no paired runs"); return None
    scenarios = sorted(diff.scenario.unique())
    fig, axes = plt.subplots(1, len(scenarios), figsize=(5 * len(scenarios), 3.3), squeeze=False, sharey=True)
    for ax, scenario in zip(axes[0], scenarios):
        part = diff[diff.scenario == scenario]
        names = plots.order_classes(part.name.unique())
        for k, policy in enumerate(["reeval", "reeval_preempt"]):
            stats = plots.mean_ci(part[part.policy == policy], "delta", "name").set_index("name").reindex(names)
            ax.bar(np.arange(len(names)) + 0.4 * k - 0.2, stats["mean"], 0.4, yerr=stats["ci"], label=plots.POLICY_LABELS[policy])
        ax.axhline(0, color="black", lw=0.8)
        ax.set_xticks(range(len(names))); ax.set_xticklabels(names, rotation=30, ha="right", fontsize=8); ax.set_title(scenario)
    axes[0][0].set_ylabel("Δ " + label + " vs off"); axes[0][-1].legend(fontsize=7)
    plt.tight_layout()
    return diff

d_pdr = delta_plot("qos_pdr", "QoS-PDR")
d_col = delta_plot("collision_rate", "collision rate")
d_dt = delta_plot("dt_mean_ms", "mean DT [ms]")
d_aoi = delta_plot("aoi_mean_ms", "mean AoI [ms]")
''')
nb.md('The same differences as a table, with the interval and whether it excludes 0:')
nb.code('''
rows = []
for value, diff in (("qos_pdr", d_pdr), ("collision_rate", d_col), ("dt_mean_ms", d_dt), ("aoi_mean_ms", d_aoi)):
    if diff is None:
        continue
    stats = plots.mean_ci(diff, "delta", ["scenario", "policy", "name"])
    stats["metric"] = value
    rows.append(stats)
table = pd.concat(rows)
table["significant"] = (table["mean"].abs() > table["ci"]) & table["ci"].notna()
table.pivot_table(index=["scenario", "name"], columns=["metric", "policy"], values="mean").round(4)
''')

nb.md(r'''## 3. Who pays for pre-emption

Pre-emption moves a lower-priority resource out of the way of a higher-priority reservation. If it
works as intended, the gain of the important classes is larger than the loss of the less important
ones. Below, Δ QoS-PDR of `reeval_preempt` against `reeval`, the effect of pre-emption alone,
against the SL priority of the class.''')
nb.code('''
diff = plots.paired_difference(flows, "qos_pdr", baseline="reeval")
diff = diff[diff.policy == "reeval_preempt"]
if len(diff):
    priority = flows.drop_duplicates("name").set_index("name").priority
    diff["priority"] = diff.name.map(priority)
    stats = plots.mean_ci(diff, "delta", ["scenario", "priority", "name"])
    for scenario, g in stats.groupby("scenario"):
        plt.errorbar(g.priority, g["mean"], yerr=g["ci"], marker="o", capsize=3, label=scenario)
    plt.axhline(0, color="black", lw=0.8); plt.xlabel("SL priority (1 = most important)")
    plt.ylabel("Δ QoS-PDR, pre-emption on vs off"); plt.legend(fontsize=7); plt.title("Effect of pre-emption alone")
''')

nb.md(r'''## 4. PER targets met

A cell is the share of runs in which the measured PER (1 − QoS-PDR over all attempts up to
1000 m) is at or below the PQI target. Most targets are far out of reach without retransmissions,
so the second table shows how many times above its target each class stays.''')
nb.code('''
met = flows.groupby(["scenario", "name", "policy"]).per_met.mean().unstack()
met.reindex(columns=[p for p in plots.POLICY_ORDER if p in met.columns]).round(2)
''')
nb.code('''
flows["per_ratio"] = flows.per_measured / flows.per_target
ratio = flows.groupby(["scenario", "name", "policy"]).per_ratio.mean().unstack()
ratio.reindex(columns=[p for p in plots.POLICY_ORDER if p in ratio.columns]).round(1)
''')

nb.md(r'''## 5. Hidden-node losses against sensing-visible ones

Re-evaluation and pre-emption only use what the UE sensed. They cannot help against interferers the
sender never decoded. Splitting the collision losses shows which part moved.''')
nb.code('''
c = s.causes[s.causes.cause.isin(["hidden-node collision", "sensing-visible collision"])]
t = c.groupby(["scenario", "policy", "cause"])["count"].sum().unstack()
attempts = s.by_distance.groupby(["scenario", "policy"]).attempts.sum()
(t.div(attempts, axis=0)).unstack("policy").round(5)
''')

nb.md(r'''## 6. Under impairments

Does re-evaluation help more, or less, when the channel is jammed, bursty, or the budget shorter?
Δ QoS-PDR of the platooning class, per condition.''')
nb.code('''
platoon = flows[(flows.scenario == "highway_platoon") & (flows.name == "platooning")]
diff = plots.paired_difference(platoon, "qos_pdr")
if len(diff):
    stats = plots.mean_ci(diff, "delta", ["condition", "policy", "density"])
    stats.pivot_table(index=["condition", "density"], columns="policy", values="mean").round(4)
''')

nb.md(r'''## 7. Reading the results

The cells above answer the questions with the numbers of the sweep shown. Keep in mind when
concluding:

* With one seed (smoke sweep) there is no interval and no conclusion.
* Δ QoS-PDR of a class is bounded by its sensing-visible collision losses under `off`: if those
  are small, nothing can improve much.
* A dynamic class (crash mitigation, emergency trajectory) is pre-empted, and pre-empts, only
  through its retransmissions: the SCI of a copy announces the next one, nothing further.
* Pre-emption re-selects with the remaining budget only, which for 20 ms platooning at T3 = 2.25 ms
  before the resource leaves a short window.
''')
nb.save(sys.argv[1])
