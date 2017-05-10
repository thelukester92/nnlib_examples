#include <iostream>
#include <iomanip>
#include <nnlib.h>
using namespace std;
using namespace nnlib;

Tensor<> extrapolate(Sequencer<> &model, const Tensor<> &context, size_t length)
{
	size_t sequenceLength = model.sequenceLength();
	size_t bats = model.batch();
	
	model.forget();
	model.sequenceLength(1);
	model.batch(1);
	
	for(size_t i = 0; i < context.size(0); ++i)
	{
		model.forward(context.narrow(0, i));
	}
	
	Tensor<> result(length, 1, 1);
	for(size_t i = 0; i < length; ++i)
	{
		result.narrow(0, i).copy(model.forward(model.output()));
	}
	
	model.sequenceLength(sequenceLength);
	model.batch(bats);
	
	return result;
}

int main(int argc, const char **argv)
{
	ArgsParser args;
	args.addInt('e', "epochs");
	args.addDouble('l', "learningRate");
	args.addDouble('d', "learningRateDecay");
	args.parse(argc, argv);
	
	size_t sequenceLength		= 150;
	size_t bats					= 20;
	size_t epochs				= args.getInt('e', 100);
	double validationPart		= 0.33;
	double learningRate			= args.getDouble('l', 0.01);
	double learningRateDecay	= args.getDouble('d', 0.999);
	
	// Bootstrap
	
	cout << "===== Training on Airline =====" << endl;
	cout << "epochs              = " << epochs << endl;
	cout << "learning rate       = " << learningRate << endl;
	cout << "learning rate decay = " << learningRateDecay << endl;
	cout << "Setting up..." << endl;
	
	RandomEngine::seed(0);
	cout << setprecision(5) << fixed;
	
	// Data loading
	
	// Tensor<> series = File<>::loadArff("data/airline.arff");
	Tensor<> series(500);
	for(size_t i = 0; i < series.size(0); ++i)
		series(i) = sin(0.05 * i);
	series.resize(series.size(0), 1);
	
	double min = series.min(), max = series.max();
	series.normalize();
	
	// Data splitting
	
	size_t trainLength = (1.0 - validationPart) * series.size(0);
	size_t testLength = series.size(0) - trainLength + 1;
	
	Tensor<> train = series.narrow(0, 0, trainLength);
	Tensor<> test = series.narrow(0, trainLength - 1, testLength);
	
	Tensor<> trainFeat = train.narrow(0, 0, trainLength - 1);
	Tensor<> trainLab = train.narrow(0, 1, trainLength - 1);
	
	Tensor<> testFeat = test.narrow(0, 0, testLength - 1);
	Tensor<> testLab = test.narrow(0, 1, testLength - 1);
	
	// Modeling
	
	Sequencer<> nn(
		new Sequential<>(
			new LSTM<>(1, 10),
			new Linear<>(1)
		),
		sequenceLength
	);
	nn.batch(bats);
	
	MSE<> critic(nn.outputs());
	Nadam<> optimizer(nn, critic);
	optimizer.learningRate(learningRate);
	
	Tensor<> preds = extrapolate(nn, trainFeat.reshape(trainLength - 1, 1, 1), testLength);
	double minError = critic.forward(preds, test.reshape(testLength, 1, 1));
	cout << "Initial error: " << minError << endl;
	
	// Training
	
	Tensor<> foo = series.narrow(0, 0, series.size() - 1);
	Tensor<> bar = series.narrow(0, 1, series.size() - 1);
	
	// SequenceBatcher<> batcher(trainFeat, trainLab, sequenceLength, bats);
	SequenceBatcher<> batcher(foo, bar, sequenceLength, bats);
	
	cout << "Training..." << endl;
	for(size_t i = 0; i < epochs; ++i)
	{
		batcher.reset();
		
		nn.forget();
		optimizer.step(batcher.features(), batcher.labels());
		optimizer.learningRate(optimizer.learningRate() * learningRateDecay);
		
		Progress<>::display(i, epochs);
		
		preds = extrapolate(nn, trainFeat.reshape(trainLength - 1, 1, 1), testLength);
		double err = critic.forward(preds, test.reshape(testLength, 1, 1));
		cout << "\terr: " << err << "\tmin: " << minError << flush;
		
		if(err < minError)
		{
			minError = err;
			
			Tensor<> seriesAndPreds(trainFeat.size(0) + preds.size(0), 3);
			seriesAndPreds.fill(File<>::unknown);
			seriesAndPreds.select(1, 0).narrow(0, 0, trainFeat.size(0)).copy(trainFeat).scale(max - min).shift(min);
			seriesAndPreds.select(1, 1).narrow(0, trainFeat.size(0), testLab.size(0)).copy(testLab).scale(max - min).shift(min);
			seriesAndPreds.select(1, 2).narrow(0, trainFeat.size(0), preds.size(0)).copy(preds).scale(max - min).shift(min);
			
			File<>::saveArff(seriesAndPreds, "pred.arff");
		}
	}
	Progress<>::display(epochs, epochs, '\n');
	
	preds = extrapolate(nn, trainFeat.reshape(trainLength - 1, 1, 1), testLength);
	cout << "Final error: " << critic.forward(preds, test.reshape(testLength, 1, 1)) << endl;
	
	return 0;
}
