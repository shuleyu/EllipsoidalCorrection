#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>

#include<ShellExecVec.hpp>
#include<FindAz.hpp>
#include<Lon2180.hpp>
#include<WayPoint.hpp>
#include<GcpDistance.hpp>
#include<EllipsoidalCorrection.hpp>

using namespace std;

struct raySurfaceData{

    double rayp, dist, tt;

    vector<double> surfacingDist;

    raySurfaceData(double d, double r, double t){

        tt = t;
        dist = d;
        rayp = r;
        surfacingDist.clear();
        surfacingDist.push_back(0);
    }
};

unordered_set<string> phaseDecorator{"ab", "bc"};

string hongyuParse(const string &s);
vector<raySurfaceData> parseTaupResult(const vector<string> &input);
pair<bool, string> checkDecorator(const string &s);
raySurfaceData chooseRay(vector<raySurfaceData> &rays, const bool &majorArc, const string &decor);
string getRidOfSuffix(const std::string &inputPhase);

int main(int argc, char * argv[]){

    if (argc != 2) {

        cout << "usage: ./EllipsoidalCorrection.out [parameterFile]\n";
        cout << "\n";
        cout << "The first row in [parameterFile] is an explanatory header (will be ignored by the program).\n";
        cout << "The rest rows are inputs, they need to have the info in each column indicated by the header line.\n";
        cout << "The outputs is stdout (print to screen), and a log file named \"log.txt\"" << endl;
        return 1;
    }

    auto ec = EllipsoidalCorrection();


    // set taup precision.
    ofstream fpTaup(".taup");
    fpTaup << "taup.depth.precision = 4\ntaup.distance.precision = 4\ntaup.latlon.precision = 4\n";
    fpTaup.close();


    // I/O.
    ifstream fpin(argv[1]);
    ofstream fpout("log.txt");

    string headerLine, line;
    getline(fpin, headerLine);

    cout  << "> phaseName    evlo(˚)    evla(˚)    evde(km)    stlo(˚)    stla(˚)    dT(sec.)" << endl;
    fpout << "> phaseName    evlo(˚)    evla(˚)    evde(km)    stlo(˚)    stla(˚)    dT(sec.)" << endl;


    // read input line by line.
    while (getline(fpin, line)) {

        if (line == "") {

            continue;
        }

        stringstream ss(line);

        string phaseName;

        double evlo, evla, evde, stlo, stla;

        // check input is the correct format.
        if (ss >> phaseName >> evlo >> evla >> evde >> stlo >> stla) {

            // process
        }
        else {

            continue;
        }


        // parse the phase name using the convention in Hongyu's paper.
        // expanding the numbers in the phase.

        string inputPhase = hongyuParse(phaseName);

        if (inputPhase == "") {

            cout  << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    phaseNameError" << endl;
            fpout << endl << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    phaseNameError" << endl;
            continue;
        }

        // check and find the decorator ("ab", "bc").

        auto decorRes = checkDecorator(inputPhase);

        if (!decorRes.first) {

            cout  << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    inconsistantDecorator" << endl;
            fpout << endl << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    inconsistantDecorator" << endl;
            continue;
        }

        string decor = decorRes.second;


        // get the ray azimuth.

        double az = FindAz(evlo, evla, stlo, stla);


        // get event/station gcp distance.

        double dist = GcpDistance(evlo, evla, stlo, stla);


        // check major arc, adjust azimuth if we need major arc

        bool majorArc = false;

        if (inputPhase.back() == 'm') {

            majorArc = true;

            dist = 360 - dist;
            az = 360 - az;

            inputPhase.pop_back();
        }

        if (majorArc) {

            fpout << endl << phaseName << ", stlo: " << evlo << ", stla: " << evla << ", depth: " << evde
                  << ", stlo: " << stlo << ", stla: " << stla << ", travel distance: " << dist << "(corrected for major arc), azimuth: " << az << "(corrected for major arc)" << endl;
        }
        else {

            fpout << endl << phaseName << ", stlo: " << evlo << ", stla: " << evla << ", depth: " << evde
                  << ", stlo: " << stlo << ", stla: " << stla << ", travel distance: " << dist << ", azimuth: " << az << endl;
        }


        // call taup.
        // get location where it reaches the surface.

        auto res = ShellExecVec("taup_pierce -h " + to_string(evde) + " -ph " + getRidOfSuffix(inputPhase) + " -deg " + to_string(dist) + " -mod ak135 -pierce 0 --nodiscon");

        auto rays = parseTaupResult(res);

        if (rays.empty()) {

            cout  << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    noSuchRayInTaup" << endl;
            fpout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    noSuchRayInTaup" << endl;

            continue;
        }


        // choose from the rays return from taup (when there's triplications), using major arc and decorator info.
        auto ray = chooseRay(rays, majorArc, decor);

        // get sections.

        auto sections = ec.parsePhaseName(inputPhase, ray.surfacingDist);

        if (sections.empty()) {

            cout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    phaseBreakDownError(maybe because distance limits)" << endl;
            fpout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    phaseBreakDownError(maybe because distance limits)" << endl;

            continue;
        }


        // start accumulate correction for each sections.

        double totalCorrection = 0, curDepth = evde, curAz = az, curEvlo = evlo, curEvla = evla, curStlo = 0, curStla = 0;

        for(auto phaseSection: sections) {

            // do correction for each segments.

            auto correctionResult = ec.getPhaseCorrection(phaseSection.first, curDepth, curEvla, phaseSection.second, curAz);

            if (correctionResult.second != 0) {

                fpout << " - "  << phaseSection.first << ", starts: lon " << curEvlo << " / lat " << curEvla << " / depth "
                      << curDepth << ", dist: " << phaseSection.second << ", az: " << curAz << ", dt: " << ec.getError(correctionResult.second) << endl;
                cout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    sourceDepthError" << endl;
                fpout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    sourceDepthError" << endl;

                totalCorrection = 0.0 / 0.0;
                break;
            }


            totalCorrection += correctionResult.first;

            fpout << " - "  << phaseSection.first << ", starts: lon " << curEvlo << " / lat " << curEvla << " / depth "
                  << curDepth << ", dist: " << phaseSection.second << ", az: " << curAz << ", dt: " << correctionResult.first << "." << endl;

            // find surfacing point.

            tie(curStlo, curStla) = WayPoint(curEvlo, curEvla, curAz, phaseSection.second);

            // find next azimuth.

            curAz = Lon2180(180 + FindAz(curStlo, curStla, curEvlo, curEvla));

            // shift to next surface bouncing point.
            curEvlo = curStlo;
            curEvla = curStla;
            curDepth = 0;
        }


        if (!isnan(totalCorrection)) {

            cout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    " << totalCorrection << endl;
            fpout << phaseName << "    " << evlo << "    " << evla << "    " << evde << "    " << stlo << "    " << stla << "    " << totalCorrection << endl;
        }
    }
    fpin.close();
    fpout.close();

    return 0;
}


string hongyuParse(const string &s) {

// expand the numbers in phase name. e.g:

// A3 -> AAA
// A3D2EB -> AAADDEB
// ABC3D2m -> ABCABCABCDDm
// 32A -> "" (error return empty string)

    string ret = "";

    int prevPosition = 0, p = 0;

    while (p < s.size()) {

        if (isdigit(s[p])) {

            int q = p;

            while (q < s.size() && isdigit(s[q])) {

                ++q;
            }

            // repeated part is s[prevPosition] ... s[p - 1]
            // number is s[p] ... s[q - 1]

            string phase = s.substr(prevPosition, p - prevPosition);

            for (int i = 0; i < stoi(s.substr(p, q - p)); ++i) {

                ret += phase;
            }

            prevPosition = p = q;
        }
        else {

            ++p;
        }
    }

    if (prevPosition != s.size()) {

        ret += s.substr(prevPosition);
    }

    return ret;
}



// this function strongly depends on the output format of taup_pierce.
// will find key words ">", "degrees", "rayParam" to locate dist and rayp.
// for the result lines, the first column will be interpreted as distance where ray hit surface.

vector<raySurfaceData> parseTaupResult(const vector<string> &input){

    vector<raySurfaceData> ret;

    for (auto line: input) {

        if (line[0] == '>') {

            stringstream ss(line);

            string prevStr, curStr;

            double dist = 0, rayp = 0, tt = 0;

            while (ss >> curStr) {

                if (curStr == "degrees") {

                    dist = stod(prevStr);
                }
                else if (curStr == "seconds") {

                    tt = stod(prevStr);
                }
                if (prevStr == "rayParam") {

                    rayp = stod(curStr);
                    break;
                }

                prevStr = curStr;
            }

            ret.push_back(raySurfaceData(dist, rayp, tt));
        }
        else {

            stringstream ss(line);

            double dist;

            ss >> dist;

            dist = abs(dist);

            // when evde = 0, there will be a 0 deg in taup result, ignore it.
            if (dist > 1e-5) {

                ret.back().surfacingDist.push_back(dist);
            }
        }
    }
    return ret;
}

pair<bool, string> checkDecorator(const string &s){

    string ret = "";

    if (s.size() <= 2) {

        return {true, ret};
    }

    int p = 0;

    while (p < s.size()) {

        while (p < s.size() && ::isupper(s[p])) {

            ++p;
        }

        int q = p;

        while (q < s.size() && ::islower(s[q])) {

            ++q;
        }

        if (phaseDecorator.find(s.substr(p, q - p)) != phaseDecorator.end()) {

            if (ret == "") {

                ret = s.substr(p, q - p);
            }
            else if (ret != s.substr(p, q - p)) {

                return {false, ret};
            }
        }
        p = q;
    }
    return {true, ret};
}


raySurfaceData chooseRay(vector<raySurfaceData> &rays, const bool &majorArc, const string &decor = "") {

    if (rays.size() == 1) {

        return rays[0];
    }

    // filter through major arc.
    // presumably this doesn't apply for phase with decorators (such as PKPab, PKPbc, etc)

    if (majorArc) {

        // sort using the first arrival.

        auto cmp = [](const raySurfaceData &d1, const raySurfaceData &d2){

            if (d1.tt == d2.tt) {

                return d1.dist < d2.dist;
            }
            return d1.tt < d2.tt;
        };

        sort(rays.begin(), rays.end(), cmp);


        for (auto ray: rays) {

            // return the first arrival on major arc.
            if (180 <= ray.dist && ray.dist <= 360) {

                return ray;
            }
        }
    }

    if (decor == "") {

        // no decorator, no major arc, with triplications.
        // just return the first arrival.
        return rays[0];
    }

    // with decorator (such as PKPab, PKPbc, etc)
    // sort according to rayp.

    auto cmp = [](const raySurfaceData &d1, const raySurfaceData &d2){

        if (d1.rayp == d2.rayp) {

            return d1.tt < d2.tt;
        }
        return d1.rayp < d2.rayp;
    };

    sort(rays.begin(), rays.end(), cmp);

    if (decor == "bc"){
        // PKPbc: smaller ray parameter.

        return rays[0];
    }
    else {

        return rays[1];
    }
}

string getRidOfSuffix(const std::string &inputPhase) {

    std::string ret = inputPhase;

    if (inputPhase.size() > 2) {

        int p = 0;

        ret = "";

        while (p < inputPhase.size()) {

            while (p < inputPhase.size() && ::isupper(inputPhase[p])) {

                ret += inputPhase[p];
                ++p;
            }

            int q = p;

            while (q < inputPhase.size() && ::islower(inputPhase[q])) {

                ++q;
            }

            if (phaseDecorator.find(inputPhase.substr(p, q - p)) == phaseDecorator.end()) {

                ret += inputPhase.substr(p, q - p);
            }

            p = q;
        }
    }

    return ret;
}
